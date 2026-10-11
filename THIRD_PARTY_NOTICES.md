# Third-Party Notices

TurboSCXML first-party code is licensed under the Apache License 2.0.

TurboSCXML depends on Salts and other libraries supplied through package
configuration or package-manager mechanisms. Those dependencies are not
relicensed by TurboSCXML and remain governed by their respective upstream
licenses.

The optional QuickJS profiles use **QuickJS-NG 0.10.1**, distributed under the
[upstream MIT license](https://github.com/quickjs-ng/quickjs/blob/v0.10.1/LICENSE).
The `quickjs` feature in `vcpkg.json` selects the
[`quickjs-ng` port at the recorded vcpkg baseline](https://github.com/microsoft/vcpkg/tree/b1b19307e2d2ec1eefbdb7ea069de7d4bcd31f01/ports/quickjs-ng).
That port fetches the upstream `v0.10.1` tag, verifies its source archive hash,
and installs the upstream copyright and license as
`share/quickjs-ng/copyright`. TurboSCXML does not vendor or locally patch the
engine. The private sandbox is TurboSCXML integration code; its bounds and
supported execution contract are documented in
[QuickJS execution quotas](docs/specs/quickjs-execution-quota.md).

The optional CHTTP feature uses GmSSL 3.2.0, Apache-2.0, through the
[qigao vcpkg registry port, revision 9](https://github.com/qigao/vcpkg-cache/tree/6115feafc309ffdd21f6cf9e53be8105b25e911e/ports/gmssl).
That port records the upstream commit and platform/crypto patches; no GmSSL
source is vendored here. This is the static provider required by the published
CHTTP/SaltsUtils Crypto link closure. The SDK's
[published qualification and audit limitations](https://github.com/qigao/chttp/releases/tag/v2.1.0-rc.3)
also apply to this dependency; TurboSCXML's loopback tests are not a cryptographic audit.
