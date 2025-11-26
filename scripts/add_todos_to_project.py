#!/usr/bin/env python3
"""
Script to add TODO items from TODO.md and BUILD_AND_TEST_SUMMARY.md to GitHub Project
"""
import subprocess
import json
import re
import requests
import os

# Project details
PROJECT_NUMBER = 4
OWNER = "Naphome"

def get_project_id():
    """Get the project ID from project number"""
    result = subprocess.run(
        ["gh", "project", "list", "--owner", OWNER, "--format", "json"],
        capture_output=True,
        text=True
    )
    projects = json.loads(result.stdout)["projects"]
    for project in projects:
        if project["number"] == PROJECT_NUMBER:
            return project["id"]
    raise Exception(f"Project {PROJECT_NUMBER} not found")

def get_github_token():
    """Get GitHub token from gh auth"""
    result = subprocess.run(
        ["gh", "auth", "token"],
        capture_output=True,
        text=True
    )
    if result.returncode != 0:
        raise Exception("Failed to get GitHub token. Run 'gh auth login' first.")
    return result.stdout.strip()

def add_draft_item(project_id, title, body="", token=None):
    """Add a draft item to the project using GraphQL API"""
    if token is None:
        token = get_github_token()
    
    mutation = """mutation($projectId: ID!, $title: String!, $body: String) {
  addProjectV2DraftIssue(input: {
    projectId: $projectId
    title: $title
    body: $body
  }) {
    projectItem {
      id
    }
  }
}"""
    
    variables = {
        "projectId": project_id,
        "title": title
    }
    
    if body:
        variables["body"] = body
    
    payload = {
        "query": mutation,
        "variables": variables
    }
    
    headers = {
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json"
    }
    
    response = requests.post(
        "https://api.github.com/graphql",
        json=payload,
        headers=headers
    )
    
    if response.status_code != 200:
        print(f"Error adding item '{title}': HTTP {response.status_code}")
        print(f"  Response: {response.text}")
        return False
    
    result = response.json()
    if "errors" in result:
        print(f"GraphQL error for '{title}': {result['errors']}")
        return False
    
    return True

def parse_todo_items():
    """Parse TODO items from TODO.md"""
    items = []
    
    with open("TODO.md", "r") as f:
        content = f.read()
    
    # Extract items from "In Progress" section
    in_progress_match = re.search(r"### In Progress 🚧\n(.*?)(?=### Backlog|$)", content, re.DOTALL)
    if in_progress_match:
        in_progress_content = in_progress_match.group(1)
        # Find all uncompleted items
        for line in in_progress_content.split("\n"):
            line = line.strip()
            if line.startswith("- [ ]"):
                # Extract the title (remove checkbox and markdown)
                title = re.sub(r"^- \[ \]\s*\*\*", "", line)
                title = re.sub(r"\*\*:\s*", ": ", title)
                title = title.strip()
                if title:
                    items.append(("In Progress", title))
            elif line.startswith("  - [ ]"):
                # Sub-item
                title = re.sub(r"^\s+- \[ \]\s*\*\*", "", line)
                title = re.sub(r"\*\*:\s*", ": ", title)
                title = title.strip()
                if title:
                    items.append(("In Progress", f"  {title}"))
            elif line.startswith("    - [ ]"):
                # Sub-sub-item
                title = re.sub(r"^\s+- \[ \]\s*", "", line)
                title = title.strip()
                if title:
                    items.append(("In Progress", f"    {title}"))
    
    # Extract items from "Backlog" section
    backlog_match = re.search(r"### Backlog 📋\n(.*?)$", content, re.DOTALL)
    if backlog_match:
        backlog_content = backlog_match.group(1)
        current_category = None
        for line in backlog_content.split("\n"):
            line = line.strip()
            if line.startswith("- [ ] **"):
                # Category item
                title = re.sub(r"^- \[ \]\s*\*\*", "", line)
                title = re.sub(r"\*\*:\s*", ": ", title)
                title = re.sub(r"\*\*$", "", title)
                title = title.strip()
                if title:
                    current_category = title
                    items.append(("Backlog", title))
            elif line.startswith("  - [ ]"):
                # Sub-item
                title = re.sub(r"^\s+- \[ \]\s*", "", line)
                title = title.strip()
                if title:
                    category_prefix = f"{current_category} - " if current_category else ""
                    items.append(("Backlog", f"{category_prefix}{title}"))
    
    return items

def parse_next_steps():
    """Parse Next Steps from BUILD_AND_TEST_SUMMARY.md"""
    items = []
    
    with open("BUILD_AND_TEST_SUMMARY.md", "r") as f:
        content = f.read()
    
    # Find the "Next Steps" section
    next_steps_match = re.search(r"## Next Steps\n(.*?)$", content, re.DOTALL)
    if next_steps_match:
        next_steps_content = next_steps_match.group(1)
        # Extract numbered items
        for line in next_steps_content.split("\n"):
            line = line.strip()
            # Match numbered list items (1., 2., etc.)
            match = re.match(r"^\d+\.\s+(.+)$", line)
            if match:
                items.append(("Next Steps", match.group(1)))
    
    return items

def main():
    print("Getting GitHub token...")
    token = get_github_token()
    print("✓ Token obtained")
    
    print("\nGetting project ID...")
    project_id = get_project_id()
    print(f"Project ID: {project_id}")
    
    print("\nParsing TODO items...")
    todo_items = parse_todo_items()
    print(f"Found {len(todo_items)} TODO items")
    
    print("\nParsing Next Steps...")
    next_steps = parse_next_steps()
    print(f"Found {len(next_steps)} Next Steps items")
    
    all_items = todo_items + next_steps
    print(f"\nTotal items to add: {len(all_items)}")
    
    print("\nAdding items to project...")
    success_count = 0
    for i, (category, title) in enumerate(all_items, 1):
        full_title = f"[{category}] {title}"
        print(f"  [{i}/{len(all_items)}] Adding: {full_title[:60]}...")
        success = add_draft_item(project_id, full_title, token=token)
        if not success:
            print(f"    ⚠️  Failed to add item")
        else:
            print(f"    ✓ Added")
            success_count += 1
    
    print(f"\n✅ Done! Added {success_count}/{len(all_items)} items to project {PROJECT_NUMBER}")

if __name__ == "__main__":
    main()
