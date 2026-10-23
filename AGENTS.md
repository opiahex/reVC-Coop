# VC:COOP native reVC branch

- This is the source-engine migration. Keep the original ASI project in ../vc-coop unchanged as the preserved prototype. Downloads/sa-coop-main remains read-only.
- Integrate through engine classes and explicit lifecycle/input/render calls. Do not add hardcoded VC addresses, binary detours, hand-written field offsets or modified vtables.
- Reuse transport, validated wire types, interpolation, chat logic and regression tests from the preserved prototype; keep third-party attribution.
- All engine access stays on the main/game thread. Transport workers exchange copied snapshots only.
- Run tools/build-coop.ps1 -Test for changed C++ batches. It builds the engine and meaningful portable/network/UI tests. Native execution is separate; report what was actually observed.
- Keep compilation products and local game assets in the chat work directory. Do not put game resources or personal saves in source/package commits.
- Preserve upstream notices. Record source URLs, revisions, build changes and tested limitations in MIGRATION.md.
- The initial migration does not imply campaign, vehicle, damage or cutscene completion. Verify each stage in two real processes before claiming it works.
