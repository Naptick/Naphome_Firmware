# Enable GitHub Pages - Quick Guide

## The Issue
The workflow ran but failed with:
```
Get Pages site failed. Please verify that the repository has Pages enabled
```

## Solution: Enable via Web UI

The GitHub API is blocked for private repos on free plans, but you can enable Pages through the web interface.

### Steps:

1. **Open Settings** (I just opened this for you):
   ```
   https://github.com/Naptick/Naphome_Firmware/settings/pages
   ```

2. **Enable GitHub Pages**:
   - Scroll to the **Pages** section
   - Under **Source**, select: **GitHub Actions**
   - Click **Save**

3. **If you see "GitHub Pages is disabled"**:
   - Your plan may not support Pages for private repos
   - Options:
     a) Upgrade to GitHub Pro ($4/month)
     b) Make repository public temporarily
     c) Use alternative hosting (Netlify, Vercel)

### After Enabling:

The workflow will automatically run and deploy your docs to:
```
https://naptick.github.io/Naphome_Firmware/
```

### Verify:

```bash
# Check Pages status
gh api repos/Naptick/Naphome_Firmware/pages -X GET

# Watch the next workflow run
gh run watch
```

## What's Already Done ✅

- ✅ Workflow file pushed to `.github/workflows/pages.yml`
- ✅ Documentation improved and pushed
- ✅ Jekyll configuration ready
- ✅ Workflow is queued and will run once Pages is enabled

You just need to enable Pages in the settings!
