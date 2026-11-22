# Contributing

- Branches, the per-game variables (`games/games.toml`), building, testing and syncing with upstream are described in
  [MAINTENANCE.md](MAINTENANCE.md). A new difference between the apps is added to `games/games.toml` first, then read
  from it; `tests/test_game_variables.py` fails when code and list disagree.
- Tests: `LC_ALL=C.UTF-8 tools/check_all.sh`. Some unit tests fail on a pristine tree too; compare with the previous
  result, not with zero.
- Never commit game files, keys, signing keys or dumps. The repository contains no game content and distributes none
  (README, [Legal](README.md#legal)).
- A change meant for upstream ([AurelioB/totk-android](https://github.com/AurelioB/totk-android)) is made on a branch
  from `upstream/main`. The GPL-3.0 Eden Duo code (`games/totk/duo/`, see
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)) and the Bifrost bridge stay out of upstream PRs.
- Changes carried from another branch are cherry-picked with `git cherry-pick -x`, keeping their authors.
