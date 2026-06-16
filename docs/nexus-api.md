# Nexus Mods API + `tools/nexus`

How to query a mod's release status and stats from the Nexus Mods public API,
and the `tools/nexus` CLI that wraps it. **The API is read-only** — there is no
public endpoint to upload a mod, edit a page, or upload images, so this is a
"is my mod out of review yet / how's it doing" tool, nothing more.

## `tools/nexus` — usage

```
nexus                     status grid of watched mods (NEXUS_MODS env, or tools/nexus.mods)
nexus status <id> [game]  release status + stats for one mod   (--full, --json)
nexus live   <id> [game]  print LIVE/NOT_LIVE; exit 0 iff live  (until-loop friendly)
nexus validate            check the API key + show rate limits
nexus help
```

- **Auth / config:** put the key in a git-ignored **`tools/nexus.env`** — `cp tools/nexus.env.example
  tools/nexus.env` and fill in `NEXUS_API_KEY` (personal key from
  <https://www.nexusmods.com/users/myaccount?tab=api>). The tool auto-sources it. `tools/env.sh` is
  **tracked**, so the key must never go there; `nexus.env` is the right home (not committed, and unlike
  `~/.bashrc` it isn't swept into config-sync'd dotfiles). An exported `NEXUS_API_KEY` in the
  environment takes precedence and skips the file.
- **Default game:** `$NEXUS_GAME` or `skyrimspecialedition`. Override per call as the 2nd arg.
- **Watchlist** (for the no-arg grid): set `NEXUS_MODS` in `nexus.env` (or the environment) —
  `"skyrimspecialedition:182628 skyrimspecialedition:<id>"` — or one `game:id` per line in
  `tools/nexus.mods` (git-ignored).
- **Needs:** `xh`, `jq`. Output is a dense text grid (no ANSI); `--json` emits raw mod objects for `jq`.
- **Exit codes:** `0` ok / live · `1` not-live (for `live`) or unexpected · `2` usage / bad key / rate-limited.
- **Wait until published:** `until nexus live 182628; do sleep 600; done`.

## API facts (verified 2026-06-16)

| | |
| --- | --- |
| REST v1 base | `https://api.nexusmods.com/v1` |
| Auth header | `apikey: <KEY>` |
| Rate limit | Read live from `X-RL-{Daily,Hourly}-Limit` — **20,000/day + 2,000/h** on a non-premium key (verified 2026-06-16; higher than Nexus's older 2,500/100 docs). `X-RL-*-Remaining` track usage; `429` on exceed. |
| getMod | `GET /v1/games/{game}/mods/{id}.json` |
| validate | `GET /v1/users/validate.json` (key check + rate headers) |
| GraphQL v2 | `https://api.nexusmods.com/v2/graphql` exists but is in-development/unstable — v1 REST is the stable choice for reads. |

**getMod fields used:** `status`, `available` (bool), `name`, `version`, `endorsement_count`,
`mod_downloads`, `mod_unique_downloads`, `updated_time`.

**`status` enum:** `published`, `not_published`, `under_moderation`, `publish_with_game`,
`hidden`, `removed`, `wastebinned`. **"Live"** = `available == true && status == "published"`.

### Under-review detection (confirmed 2026-06-16)

- A **stranger** querying a not-yet-public mod gets **404** (the API mirrors site visibility).
- The mod's **owner**, authenticated with their own `apikey`, gets **200** with the real `status` —
  **verified**: `nexus status 182628` returned `under_moderation` for the owner while the mod was
  still in review. So `nexus status`/`live` on your own key is a real "is it out of review yet" check
  (`live` exits 0 once `status == published && available`).
- While a mod is under moderation the API **omits `name` (and `summary`)**, so the tool shows `?` for
  the name until it's published; stats like `endorsement_count` are still returned.

### No write API

The only upload path Nexus offers is the official `Nexus-Mods/upload-action` GitHub Action hitting a
**v3** endpoint flagged **"FOR EVALUATION ONLY"** — not a stable public feature. Page edits and image
uploads are website-only. Treat the public API as read-only.
