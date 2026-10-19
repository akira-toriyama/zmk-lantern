## Summary

<!-- What and why. -->

## Checklist

- [ ] Commit subjects and the PR title follow glyph (`<:code:>[(scope)]<sigil> <subject>`; `glyph lint --range origin/main..HEAD`)
- [ ] `./scripts/build.sh` builds every target in build.yaml
- [ ] A change to the rail, the USB rule or Kconfig was built into canon's halves (canon `./scripts/build-zmk.sh imprint` with this tree) before merging
- [ ] Hardware claims here carry a date (a check is repeated only when its first result failed or was ambiguous)
- [ ] README / CLAUDE.md updated where the change touches them

SetStatus-task: https://github.com/akira-toriyama/projects/blob/main/.furrow/bodies/<id>.md <lane>
