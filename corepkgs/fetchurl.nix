{ system ? builtins.currentSystem
, url
, md5 ? "", sha1 ? "", sha256 ? ""
, outputHash ?
    if sha1 != "" then sha1 else if md5 != "" then md5 else sha256
, outputHashAlgo ?
    if sha1 != "" then "sha1" else if md5 != "" then "md5" else "sha256"
, executable ? false
, unpack ? false
, name ? baseNameOf (toString url)
}:

derivation {
  builder = "builtin:fetchurl";

  # New-style output content requirements.
  inherit outputHashAlgo outputHash;
  outputHashMode = if unpack || executable then "recursive" else "flat";

  inherit name system url executable unpack;

  # No need to double the amount of network traffic
  preferLocalBuild = true;

  impureEnvVars = [
    # We borrow these environment variables from the caller to allow
    # easy proxy configuration.  This is impure, but a fixed-output
    # derivation like fetchurl is allowed to do so since its result is
    # by definition pure.
    "http_proxy" "https_proxy" "ftp_proxy" "all_proxy" "no_proxy"
  ];

  # To make "nix-prefetch-url" work.
  urls = [ url ];
}
