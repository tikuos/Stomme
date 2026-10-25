# Resource service troubleshooting

Resource Inspector shows the endpoint directory inherited from its desktop.
The command client uses `--directory` or `STOMME_RESOURCE_DIRECTORY`. A namespace
path such as `/settings/ui/theme` is not a host filesystem path. See
[Resource Inspector](resource-inspector.md) for command and literal syntax.

| Result | Check and recovery |
| --- | --- |
| Endpoint unavailable | Check the selected session directory, running desktop and private directory/socket ownership. Connect to that session's endpoint. |
| Startup refused or mount collision | Preserve the host entry. Select a canonical alternate `STOMME_RESOURCE_MOUNT` with enough path space. |
| Source differs from lock | Select the matching foundation source or adopt the reviewed content pin; record development overrides separately. |
| Module identity mismatch | Restore or rebuild the host, SDK and app module as one matching set. Equal ABI values do not permit mixed SDK identities. |
| `denied` or `read_only` | Check descriptor scopes, host-control/content requirements and native owner policy. Use an authorized operation. |
| `unavailable` or `unsupported` | Check advertised product/host capabilities and current owner availability. |
| `stale_revision`, retired owner or stale cursor | Discover the current instance and obtain a fresh descriptor/snapshot/page. Retain an uncertain mutation's request ID. |
| Watch overflow | Discard deltas and obtain a new atomic snapshot/subscription. |
| `unknown_outcome` | Query the retained request receipt or job in its originating session. Do not automatically replay the mutation. |
| Pending confirmation | Complete the native confirmation flow. A queued request has not established a completed effect. |
| Save failure | Keep the native error and desired/persisted/applied distinction. Correct the filesystem condition, then refresh before a new proposal. |
| Inspector requires refresh | Inspect the current path/endpoint again; a changed or retired observation cannot authorize another proposal. |

A request may outlive its submitting connection. Cancellation requests can race
with effects; query the retained terminal status. A session restart can retire
receipts, subscriptions and native instance identities. An absent receipt after
restart does not establish that the earlier effect did not happen.

Inspect the native owner's result and structured error before changing data.
Keep preferences and documents outside build cleanup and dependency replacement.
For provider failures, record product/host, content pin, SDK identity, canonical
path, descriptor version, request ID, result/effect and the reproducing input.
Omit private document and clipboard content from diagnostic reports unless that
content is needed and authorized for the report.
