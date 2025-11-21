# Publishing Documentation to GitHub Pages

## Quick Start

Your documentation is ready to be published to GitHub Pages! Here's what has been set up:

### ✅ What's Ready

1. **GitHub Actions Workflow** (`.github/workflows/pages.yml`)
   - Automatically builds and deploys on push to `main`
   - Uses Jekyll to render Markdown
   - Triggers when `docs/` files change

2. **Jekyll Configuration** (`docs/_config.yml`)
   - Minimal theme for clean docs
   - Properly configured for `Naphome_Firmware` repository
   - Navigation and metadata set up

3. **Improved Documentation**
   - Enhanced AWS IoT interface docs (482 lines)
   - Enhanced Matter interface docs (413 lines)
   - Updated index with quick start guides
   - All Kconfig options documented
   - Complete usage examples and troubleshooting

## Steps to Publish

### Option 1: Commit and Push (Recommended)

```bash
# Commit the documentation and workflow
git add docs/ .github/workflows/pages.yml GITHUB_PAGES_SETUP.md
git commit -m "Add improved IoT/Matter documentation with GitHub Pages deployment"
git push origin main
```

### Option 2: Review First

```bash
# See what will be committed
git status
git diff --cached

# Then commit when ready
git commit -m "Add improved IoT/Matter documentation with GitHub Pages deployment"
git push origin main
```

## Enable GitHub Pages

After pushing, enable GitHub Pages:

1. **Go to Repository Settings**
   - Navigate to: https://github.com/Naphome/Naphome_Firmware/settings/pages

2. **Configure Source**
   - Under **Source**, select: **GitHub Actions**
   - (Not "Deploy from a branch")

3. **Save**
   - Click **Save**
   - GitHub will use the workflow in `.github/workflows/pages.yml`

## Verify Deployment

1. **Check Actions Tab**
   - Go to: https://github.com/Naphome/Naphome_Firmware/actions
   - Look for "Deploy GitHub Pages" workflow
   - Should complete in 2-3 minutes

2. **Access Your Docs**
   - Once deployed, visit: https://naphome.github.io/Naphome_Firmware/
   - Documentation will be live and searchable

## What's Included

The published documentation will include:

- **Quick Start** guides for AWS IoT and Matter
- **Complete API documentation** with examples
- **Configuration reference** (all Kconfig options)
- **Troubleshooting guides** for common issues
- **Architecture diagrams** and integration patterns
- **Version compatibility** information

## Troubleshooting

If the workflow fails:

1. Check the **Actions** tab for error messages
2. Verify `docs/_config.yml` has correct `baseurl` (should be `/Naphome_Firmware`)
3. Ensure all Markdown files are valid
4. Check repository settings → Pages → Source is set to "GitHub Actions"

## Manual Trigger

You can manually trigger deployment:

1. Go to **Actions** → **Deploy GitHub Pages**
2. Click **Run workflow**
3. Select `main` branch
4. Click **Run workflow**

## Next Steps

After publishing:
- Share the documentation URL with your team
- Update any external links to point to GitHub Pages
- Documentation will auto-update on every push to `main`
