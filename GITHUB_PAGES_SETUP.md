# GitHub Pages Setup Instructions

This document provides instructions for publishing the Naphome Firmware documentation to GitHub Pages.

## What Has Been Set Up

✅ **GitHub Actions Workflow** (`.github/workflows/pages.yml`)
   - Automatically builds and deploys documentation on push to `main` branch
   - Triggers when files in `docs/` directory change
   - Uses Jekyll to render Markdown files

✅ **Jekyll Configuration** (`docs/_config.yml`)
   - Minimal theme for clean documentation
   - Configured for GitHub Pages
   - Proper navigation setup

✅ **Documentation Files**
   - All documentation in `docs/` directory
   - `index.md` as landing page
   - Improved IoT and Matter interface documentation

## Steps to Enable GitHub Pages

### 1. Commit and Push the Documentation

```bash
# Stage the documentation and workflow files
git add docs/ .github/workflows/pages.yml

# Commit the changes
git commit -m "Add improved IoT and Matter documentation with GitHub Pages setup"

# Push to main branch
git push origin main
```

### 2. Enable GitHub Pages in Repository Settings

1. Go to your GitHub repository: https://github.com/Naphome/Naphome_Firmware
2. Click **Settings** (top right of repository)
3. Scroll down to **Pages** in the left sidebar
4. Under **Source**, select:
   - **Source**: `GitHub Actions`
   - This will use the workflow we created
5. Click **Save**

### 3. Wait for Deployment

- GitHub Actions will automatically run the workflow
- Check the **Actions** tab in your repository to see the deployment progress
- First deployment may take 2-3 minutes
- Subsequent deployments are faster

### 4. Access Your Documentation

Once deployed, your documentation will be available at:
```
https://naphome.github.io/Naphome_Firmware/
```

Or if your repository name is different, replace `Naphome_Firmware` with your actual repository name.

## Troubleshooting

### Workflow Not Running

- Check that GitHub Pages is enabled in repository settings
- Verify the workflow file is in `.github/workflows/pages.yml`
- Check the **Actions** tab for any error messages
- Ensure you have push permissions to the `main` branch

### Build Failures

- Check Jekyll build logs in the Actions tab
- Verify all Markdown files are valid
- Check `docs/_config.yml` for syntax errors
- Ensure `baseurl` in `_config.yml` matches your repository name

### Pages Not Updating

- Wait a few minutes for GitHub to propagate changes
- Clear browser cache
- Check the Actions tab to confirm deployment succeeded
- Verify the workflow ran after your last push

## Manual Deployment

You can also trigger the workflow manually:

1. Go to **Actions** tab in GitHub
2. Select **Deploy GitHub Pages** workflow
3. Click **Run workflow** button
4. Select branch (usually `main`)
5. Click **Run workflow**

## Documentation Structure

The documentation includes:

- **index.md** - Main landing page with quick start
- **aws_iot_interface.md** - Complete AWS IoT Core integration guide
- **matter_interface.md** - Matter bridge for sensor telemetry
- **specifications.md** - Firmware requirements
- **naphome_voice_assistant.md** - Voice assistant implementation
- **i2s_farfield_analysis.md** - Microphone analysis

## Updating Documentation

Simply edit the `.md` files in `docs/` and push to `main`:

```bash
# Edit documentation files
# Then commit and push
git add docs/
git commit -m "Update documentation"
git push origin main
```

GitHub Actions will automatically rebuild and redeploy.
