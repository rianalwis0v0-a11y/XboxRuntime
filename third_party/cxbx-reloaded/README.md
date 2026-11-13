# Third-party notices and mixed licensing

## Project licensing model

XboxRuntime will use a mixed-licence structure:

- **MIT** applies to independently authored XboxRuntime components whose copyright holders have granted MIT rights and which are not derived from GPL-covered code. The repository's existing root MIT licence remains for those eligible original files.
- **GPL-2.0** applies to Cxbx-Reloaded files copied or adapted into this project, subject to each file's exact upstream licence notice. Preserve upstream copyright and licence headers and include the corresponding licence text.
- **Combined distribution:** integrating GPL-covered components into one combined program can trigger GPL requirements for the combined derivative work. A directory boundary or calling convention does not automatically avoid those requirements. Review the architecture and distribution obligations before publishing binaries.

Do not relabel Cxbx-derived code as MIT. Do not replace or broaden the root MIT licence until all relevant contributors' relicensing permissions are confirmed. This mixed model is not a claim that every file or the combined binary can be offered under either licence at the user's choice.

## Cxbx-Reloaded

- Project: Cxbx-Reloaded, Original Xbox emulator
- Upstream: https://github.com/Cxbx-Reloaded/Cxbx-Reloaded
- License: GNU General Public License, version 2 (check each individual file; some may say version 2 or later)
- License text: [COPYING](COPYING)

No Cxbx-Reloaded implementation source has been copied into the runtime build by this preparation PR. Every imported or adapted file must retain its upstream copyright and GPL notices, identify modifications and dates, and be recorded here with its upstream path and revision.

Before release, review GPL-2.0 obligations and the final linking/distribution design with qualified legal advice.
