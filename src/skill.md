---
name: spire
description: |
  Work with Spire progress billing projects (.spire files): set up a
  schedule of values, bill each line's percent complete per period, manage
  change orders and retainage, import Plumb takeoff CSVs, and export the
  pay application (G702/G703 format) as a PDF. Use for any request about a
  pay application, progress billing, schedule of values, retainage, or a
  .spire project.
---

# Spire

A Spire project is one `.spire` file (SQLite) holding the project details,
the schedule of values, change orders, and every application for payment.
Change it only through the `spire` command; none of its commands need a
display.

## How billing works

- The schedule of values is a list of lines, each with a scheduled value.
  Lines are named by their code (`03-100`), their item number (`5`), or `CO3`
  for change order 3. `spire lines` lists them.
- Each application (period) records every line's work completed **to date**
  and materials stored. This period is to date minus the previous
  application. `spire start` begins the next application with the previous
  one's numbers carried over.
- `spire submit` fixes an application's amounts. After the first one is
  submitted the original schedule of values is locked; changes go through
  change orders. Only the latest application can be reopened.
- A change order is billed from the application it is approved on.

## Commands

- `spire new job.spire --name "Harbor St" --owner "..." --contractor "..."
  --architect "..." --contract-number HS-1 --contract-date 2026-03-02
  --retainage 10` creates a project.
- `spire info job.spire`, `spire set-info job.spire --retainage 5 ...`
- `spire add-line job.spire --description "Footings" --scheduled 65625
  [--code 03-100 --quantity 52.5 --unit m3 --unit-price 1250]`
- `spire set-line job.spire 03-100 --scheduled 70000`,
  `spire remove-line job.spire 03-100`
- `spire import job.spire takeoff.csv [--dry-run]` reads a Plumb export.
  Columns are found by heading: Code, Description, Quantity, Unit, Unit
  price, Amount, Installed. Matching lines are updated, new ones added while
  the schedule is open, and an Installed column bills the open application.
  Run with `--dry-run --json` first and show the person what would change.
- `spire start job.spire [--period-to 2026-05-31]` starts the next
  application; `spire periods job.spire` lists them.
- `spire bill job.spire 05-200 --percent 85` (or `--this-period 12000`,
  `--installed 14.4`, `--stored 8000`, `--complete`) bills a line on the
  latest application, or the one given with `--period N`.
- `spire set-period job.spire --retainage 5 --retainage-stored 0
  --application-date 2026-05-31`
- `spire change-order job.spire --description "Canopy" --amount 14250
  [--approve]`; `spire approve job.spire 3 [--undo]`;
  `spire change-orders job.spire`
- `spire summary job.spire [--period N]` gives the certificate's nine lines,
  including the current payment due.
- `spire submit job.spire`, `spire reopen job.spire`
- `spire export job.spire application.pdf [--period N]` writes the pay
  application PDF.

Amounts are in the project's currency, written plainly (`12500.50`).
Every command takes `--json`, exits 0 on success and 1 on failure, and writes
errors to stderr. `spire <command> --help` lists its options.

## Working with the person

- Submitting is the step that fixes numbers sent to an owner; confirm with
  the person before running `spire submit`.
- If they have the project open in the app, it shows your changes the next
  time it is reopened; tell them to reopen it.
- `spire open job.spire` opens the app for them; it needs their desktop, so
  do not wait on it.
