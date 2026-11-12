# Coding standards — review rules

Rules for the reviewer agent (diff-first). Implementation guidance lives in
`AGENTS.md` and `docs/planning/`.

## Docs track the code in the same diff

A diff touching `src/modules/*` or `include/board_pins.h` must also update:

- the matching ticket checkboxes/status under `docs/planning/issues/`;
- the ticket status table in `docs/planning/README.md`;
- every doc that quotes the changed symbol or call (`docs/*.md`) — grep the
  symbol name before approving.

## Quantitative claims come from the source

Counts of third-party tables, carrier frequencies, table sizes: count or quote
them from the source file in the same review. "Roughly", "about", or a number
recalled from memory is a blocking comment.

## License boundary

Code or data copied from the Bruce tree (AGPL-3.0) or CC BY-SA sources is a
block. Accept only independently written code with the reference noted in a
comment; see `docs/planning/map.md` § License.
