# Publishing this wiki

*(Developer doc.)* The pages you're reading live as Markdown in the **`wiki/` folder of the main repo**.
That folder is the source of truth — edit it in a normal pull request like any other code. GitHub's
**Wiki tab** (`github.com/coreflake1/guppyscreen/wiki`) is a *separate* git repository
(`...guppyscreen.wiki.git`), so the files have to be copied across to actually appear there.

This page explains how that copy happens and the conventions to follow so it doesn't break.

## Why it's set up this way

- **Source of truth = `wiki/` in the main repo.** Edits go through PR review, ship with the code change
  that motivated them, and are versioned alongside the release.
- **The Wiki tab is a publish target**, not where you edit. Don't edit pages directly in the GitHub Wiki
  UI — those changes live only in the `.wiki.git` repo and get overwritten on the next sync.

## Link & filename conventions (important)

The GitHub Wiki maps a file like `Calibration-Explained.md` to the page URL `…/wiki/Calibration-Explained`.

- **Link to other pages without the `.md` and without a path:** `[text](Calibration-Explained)`, not
  `[text](Calibration-Explained.md)` or `[text](wiki/Calibration-Explained.md)`. The `.md`/path form
  works when browsing the repo but **404s on the Wiki tab** — and the Wiki tab is the canonical home.
- **`_Sidebar.md`** is the left-hand navigation. **`Home.md`** is the wiki landing page.
- Use dashes in filenames; spaces in the page title (the `# H1`) are fine.

## Publishing — manual method (works today)

```sh
# one-time: clone the wiki repo (must have at least one page created via the Wiki tab first)
git clone https://github.com/coreflake1/guppyscreen.wiki.git /tmp/guppyke-wiki

# each publish:
cp wiki/*.md /tmp/guppyke-wiki/
cd /tmp/guppyke-wiki
git add -A && git commit -m "Sync wiki from main@<short-sha>" && git push
```

> GitHub requires the wiki to have **one initial page** (create any page once from the Wiki tab) before
> `.wiki.git` exists to clone.

## Publishing — opt-in CI (recommended once the wiki exists)

Drop this in `.github/workflows/wiki.yml` to auto-sync on every push to `main` that touches `wiki/`:

```yaml
name: Publish wiki
on:
  push:
    branches: [main]
    paths: ['wiki/**']
permissions:
  contents: write
jobs:
  publish:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Sync wiki/ to the GitHub Wiki
        uses: Andrew-Chen-Wang/github-wiki-action@v4
        with:
          path: wiki/
          token: ${{ secrets.GITHUB_TOKEN }}
```

It's intentionally **not enabled yet** — turn it on after the Wiki tab has its first page, and after a
manual sync has confirmed the pages render correctly. Until then, use the manual method above.
