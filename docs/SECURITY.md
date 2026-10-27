# Security

Dako is native C++20/Win32 code with no embedded web engine, script runtime, server or external binary plugin
loader. Network responses and imported settings are untrusted input. HTTPS certificate checks remain enabled;
automatic redirects are disabled, responses/imports are bounded to 2 MiB, and settings imports validate their
format before committing. Private request URLs and setting values are excluded from the audit ledger.

Optional capabilities require module consent. Imported consent is discarded. Desktop suppression has its own
consent and recovery guard; importing preferences cannot grant that consent. Managed preference writes are
journaled before application. Unknown future settings schemas are not overwritten. Hook installation/removal
and native resource baselines are exercised by --module-check.

No paid signing service or certificate has been purchased. Development executables remain unsigned. Trusted
release signing, signed update/rollback, installer distribution and their security review remain release work;
an unsigned development ZIP must not be described as a trusted signed release.

## Reporting

A private security-reporting address has not yet been configured by the maintainer. A public release needs an
approved private reporting contact. Do not publish secrets, private ICS URLs, recovery contents or exploit
details in public issues. Provide the affected version and a minimal reproduction through the eventual private
contact. No license or pricing decision is inferred from the instruction to avoid paid services.
