> Historical record. The V4 foundation plan supersedes conflicting product instructions.

# Indexed filenames (3.7 development)

The optional `files` module uses Windows Search's documented native query interfaces. It does not walk a
filesystem tree or search document contents. Input is a literal filename prefix, escaped inside fixed
read-only SQL. At most 50 names/URLs return; only validated local drive paths reach the parent process.
Missing indexing, an unindexed folder or a slow provider is shown explicitly, with a native picker fallback.

The palette starts a cancellable 200 ms debounce only for `files ` plus at least two characters. The island
uses the same module/query state. Each request runs in the existing owned helper process, inside its single
process, kill-on-close job with a 64 MiB commit ceiling. ADO uses a client cursor, one-second connection and
four-second command limits; the parent enforces a five-second overall query limit. Cancellation kills the
owned helper. No background query or sampling timer is installed. There is no COM provider call on the UI thread.

Only names and file URLs are selected. UNC/device paths, alternate streams, relative paths, malformed payloads
and oversized results are rejected. Script, shortcut and installer launches require an explicit confirmation
with the complete path. Opening any file delegates to its Windows handler; this is not a sandbox for that app.
Queries and result paths remain in memory and are excluded from learned ranking, settings and audit text.
The capability ledger reports the active index operation without its query or results. A picker dialog uses
the existing modal guard so it cannot dismiss its owner during shell message pumping.

The read-only host spike queried a newly generated, random fixture name. It returned zero rows successfully
and the helper exited. It did not enumerate known private filenames, launch files or access clipboard content.
Initial sandbox access was denied. An approved probe outside that sandbox reached the running service but hit
the original two-second timeout. The final client-cursor/four-second configuration succeeded. This verifies the
provider path on this host, not a positive-match test for every indexed folder or a guarantee of query latency.
Synthetic tests cover the result transport and filename/path boundaries without reading the actual index.

References: Microsoft's [native Windows Search query interfaces](https://learn.microsoft.com/en-us/windows/win32/search/using-sql-and-aqs-to-query-the-index)
and [LIKE predicate](https://learn.microsoft.com/en-us/windows/win32/search/-search-sql-like).
