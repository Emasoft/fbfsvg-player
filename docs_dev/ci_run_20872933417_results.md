# CI Run 20872933417 Results

**Repository:** Emasoft/fbfsvg-player  
**Branch:** main  
**Trigger:** push  
**Status:** FAILURE  
**URL:** https://github.com/Emasoft/fbfsvg-player/actions/runs/20872933417

## Job Results

| Job | Status | Duration |
|-----|--------|----------|
| API Header Test | PASS | 8s |
| macOS Build | FAIL | 8s |
| Windows Build | FAIL | 13s |
| iOS Build | FAIL | 8s |
| Linux Build | FAIL | 5s |
| Build Summary | FAIL | 4s |

## Root Cause

All build jobs failed at the **Checkout** step due to a **git submodule error**:

```
remote error: upload-pack: not our ref 32d054b091cfbb4bd80205217727d74d82f2763f
Fetched in submodule path 'skia-build', but it did not contain 32d054b091cfbb4bd80205217727d74d82f2763f.
Direct fetching of that commit failed.
The process 'git' failed with exit code 128
```

## Analysis

The `skia-build` submodule is pointing to commit `32d054b091cfbb4bd80205217727d74d82f2763f` which does not exist in the remote repository. This could happen if:

1. The commit was force-pushed away or the branch was reset
2. The submodule reference was updated to a commit that was never pushed
3. The referenced repository changed its history

## Fix Required

Update the `skia-build` submodule to point to a valid commit:

```bash
cd skia-build
git fetch origin
git checkout <valid-commit-or-branch>
cd ..
git add skia-build
git commit -m "Fix skia-build submodule reference"
git push
```

Or deinitialize and reinitialize the submodule:

```bash
git submodule deinit -f skia-build
git submodule update --init --recursive
```

## Full Annotations

- macOS Build: The process '/opt/homebrew/bin/git' failed with exit code 128
- Windows Build: The process 'C:\Program Files\Git\bin\git.exe' failed with exit code 128  
- iOS Build: The process '/opt/homebrew/bin/git' failed with exit code 128
- Linux Build: The process '/usr/bin/git' failed with exit code 128

All failures are identical: submodule checkout failed due to missing commit reference.
