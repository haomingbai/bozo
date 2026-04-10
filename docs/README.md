# bozo Documentation

This repository keeps its top-level README focused on build and packaging.
Detailed task behavior lives under `docs/`.

## Read This First

- If you want to use the task API:
  [PostgreSqlTask Guide](guides/postgresql-task.md)
- If you need to understand or change lifecycle behavior:
  [PostgreSqlTask Design](design/postgresql-task.md)

## Document Map

### Guides

- [PostgreSqlTask Guide](guides/postgresql-task.md)
  - public API behavior
  - factories, requests, transactions, and pool semantics
  - output ownership and common failure handling

### Design

- [PostgreSqlTask Design](design/postgresql-task.md)
  - runtime architecture
  - public state machine
  - queued admission rules based on scheduled state
  - close, cancellation, and failure propagation rules
  - file map and maintenance checklist

### Compatibility

- [Legacy manual path](manual/postgresql-task.md)
  - kept as a redirect for existing links
