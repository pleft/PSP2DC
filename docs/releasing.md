# Making a Release

Releases are made by CI from a tag, as in PSPExp, so that what people download is built from public things only: the sources at that tag, joypad-os at its pinned commit with `firmware/patches`, and the toolchains' own images.

## Versions

Semantic versions, `v1.0.0` first. Then `v1.1.0` for something new, `v1.0.1` for fixes only, `v2.0.0` only when an older PSP application and adapter stop working with a new one of the other. The PSP application says which it is on Settings > Details (`PSP2DC 1.0.0`; a build that is no release says `dev` and its commit: `scripts/version.sh`).

The adapter's protocol number and the companion's numbers (the library's version, the PSP's level) are their own and change only with what they describe (`docs/psp2dc-companion.md`, G.3). A release's PSP application and adapter firmware go together.

## What a release is

One file, `PSP2DC-X.Y.Z.zip`, and its SHA-256:

| In it | What |
|---|---|
| `psp/PSP/GAME/PSP2DC/` | the PSP application and its USB driver |
| `adapter/psp2dc_rp2040zero-X.Y.Z.uf2` | the adapter's firmware |
| `dreamcast/` | disc images of the companion's demo, test and samples |
| `licences/` | `LICENSE`, `THIRD_PARTY.md` and the texts of `licenses/` |

`scripts/make-release.sh X.Y.Z` makes the same on the maintainer's machine, for trying.

## The steps

1. **Everything for it on `main`,** pushed, and `build.yml` green for that commit.
2. **The notes:** `CHANGELOG.md`'s top section named `## X.Y.Z (date)`, with what is new and what is known not to work. The site's front page when it says something the release changes (`python3 site/build.py`, looked at). Committed and pushed.
3. **A dry run:** `gh workflow run release.yml -f tag=vX.Y.Z -f publish=false` makes a draft Release with the zip. Its PSP application already says `X.Y.Z`.
4. **The draft's zip tried on hardware,** since CI's builds are not the files that were tried before: the `.uf2` flashed, the PSP application installed (`scripts/install-psp.sh <the zip's psp/PSP/GAME/PSP2DC>`), a game played, a save made and kept, and the companion's test disc run to its end. Settings > Details says `PSP2DC X.Y.Z`.
5. **`git tag vX.Y.Z && git push origin vX.Y.Z`:** the tag's run builds again, replaces the draft's files and publishes the Release.
6. **Checked:** `gh release view vX.Y.Z --json isDraft,assets` (not a draft, two files); the zip's SHA-256 is the one beside it.

A draft that is not wanted: `gh release delete vX.Y.Z` (a draft has no tag until it is published).

## What CI does not do

- **The tests** (`scripts/dev/companion-test.sh`, `companion-samples.sh`, `maple-rx-test.sh`, the card tests) run on the maintainer's machine before step 1: the simulator's build uses a macOS linker.
- **Hardware:** step 4 is by hand.
- **Pinning:** joypad-os is pinned by commit and the Arm compiler by version (13.3.rel1); the pspdev and KallistiOS images are whatever is `latest` on the day.
