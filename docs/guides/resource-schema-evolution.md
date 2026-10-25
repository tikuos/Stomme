# Resource schema evolution

Protocol version, application ABI, SDK content identity and descriptor schema
version identify different contracts. Protocol negotiation selects framing and
features. ABI 5 identifies the native application descriptor. An SDK identity
binds compiled modules to selected shared/product implementation and flags.
A descriptor's `schema_version` identifies its public resource shape. Matching
one identifier does not establish compatibility for the other contracts.

## Review changes

An additive metadata field preserves a client that ignores unknown fields when
its operation's validation and serialized value remain unchanged. Removing a
field, changing a value type, units, persistence, bounds, enum choices, action
arguments or interpretation requires review of affected clients and native
owners. Keep advertised capabilities aligned with implemented operations.
Do not silently reinterpret an existing setting or change a state into an action.

A collection refinement records how existing editable values map to discovered
children. A stream refinement records complete capture format, revision and
limits. Such refinements require updated bindings and integration evidence;
renaming a writable setting as unsupported does not close its coverage.

Provider incarnations and revisions remain runtime preconditions. A replacement
provider establishes a new incarnation, and a retired path cannot be addressed
with a prior native or document identity. Clients refresh descriptors and
snapshots after stale identities, catalog changes or watch overflow. They
reconcile uncertain mutation receipts before deciding whether to submit again.

## Persistence and downgrade

A resource schema change does not itself migrate preference or document files.
The native owner defines those formats. Review saved-data compatibility and
confirmation behavior with the resource contract. If a format changes, document
its migration, reader versions, downgrade policy and recovery procedure before
adopting the dependency. Preserve copies of user data outside generated output.

A compatible rollback restores matching foundation source, lock/profile, SDK,
host and application modules together. It preserves preference/document bytes
and verifies that the preceding native owner can reopen them. A test with an
additive descriptor change establishes that case's compatibility; it does not
establish compatibility with unrelated historical releases.
