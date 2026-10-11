# Third-Party Notices

TurboSCXML first-party code is licensed under the Apache License 2.0.

TurboSCXML depends on Salts and other libraries supplied through package
configuration or package-manager mechanisms. Those dependencies are not
relicensed by TurboSCXML and remain governed by their respective upstream
licenses.

The optional CHTTP feature uses GmSSL 3.2.0, Apache-2.0, through the
[qigao vcpkg registry port, revision 9](https://github.com/qigao/vcpkg-cache/tree/6115feafc309ffdd21f6cf9e53be8105b25e911e/ports/gmssl).
That port records the upstream commit and platform/crypto patches; no GmSSL
source is vendored here. This is the static provider required by the published
CHTTP/SaltsUtils Crypto link closure. The SDK's
[published qualification and audit limitations](https://github.com/qigao/chttp/releases/tag/v2.1.0-rc.3)
also apply to this dependency; TurboSCXML's loopback tests are not a cryptographic audit.
