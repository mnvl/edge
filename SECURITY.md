# Security

edge loads files that may come from untrusted sources: glTF and GLB models, `.edge` files, DDS, PNG and JPEG images,
and TrueType fonts. A file that crashes edge, reads or writes out of bounds, or makes it allocate without limit is a
security bug.

To report one, use GitHub's private vulnerability reporting (the repository's Security
tab, "Report a vulnerability") with a description and, if possible, the file that reproduces it. Please do
not open a public issue for it.
