# 0006: Code and model licensing

**Status:** Proposed. The project owner has yet to confirm the licence.

## Context

The project is open source and non-commercial. Some useful models allow only
non-commercial use, which conflicts with open-source licences if they are bundled.
Some useful libraries are GPL, such as PyMaxflow.

## Decision

- License StreetPano's code under Apache-2.0, which includes a patent grant.
- Never include model weights in the repository. Each is a separate download, with
  its terms shown clearly.
- Never make a non-commercial model or GPL library a required dependency.

## Consequences

- Others can reuse the code freely, including commercially. Choose a copyleft or
  non-commercial licence instead if that isn't wanted. Note that a non-commercial
  restriction would mean the project no longer meets the usual definition of open
  source.
- A graph-cut implementation with a compatible licence is needed.
- Users of non-commercial models must follow those models' terms themselves.
