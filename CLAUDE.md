# Claude Code guidelines for hpgl-viewer

## Documentation

Whenever new functionality is added — new UI controls, keyboard shortcuts, CLI flags, file formats, or behaviour changes — update `README.md` in the same commit:

- New keyboard shortcut → add a row to the shortcuts table
- New feature → add a bullet to the Features section
- Changed build dependency → update the Dependencies section
- Removed feature → remove or update the relevant entry

Do not leave documentation out of sync with the code.

## Commits

After completing each feature or fix, create a git commit immediately. Do not batch multiple features into one commit. Each commit should be self-contained and buildable.
