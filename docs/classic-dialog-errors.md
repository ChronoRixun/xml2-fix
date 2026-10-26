# Save errors in fixed native dialogs

The native message box wraps at spaces. An absolute save path can be a single
word wider than the content rectangle, particularly with the classic menu font.
The UI message formatter reduces absolute LC save paths to their leaf filenames
before appending the failure reason. File identification, backup suffixes, the
error cause, native result codes and retry/cancel behavior remain intact.

This affects only the displayed message. The pending refusal, protection ledger
and diagnostic log retain their original full reasons and paths. There are no new
hooks, save fields, protocol rules or version changes. Synthetic regressions cover
drive-letter paths, forward slashes, UNC paths, backup names and relative names.
