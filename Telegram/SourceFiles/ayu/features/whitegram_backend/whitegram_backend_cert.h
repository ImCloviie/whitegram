// This is the source code of Whitegram for Desktop.
//
// Copyright @ImCloviie, 2026
#pragma once

namespace Whitegram::Backend {

// The self-signed TLS certificate of the Whitegram backend server. A
// bare IP cannot get a CA-signed certificate, so the client pins this
// exact certificate as the only trust root for backend requests. It is
// public by design: every TLS handshake serves it to anyone who asks;
// the matching private key never leaves the server.
inline constexpr char kBackendCertificatePem[] = R"(-----BEGIN CERTIFICATE-----
MIIBpDCCAUqgAwIBAgIUJ9rNhlTB0R8hw1MuzMCiuDch2F0wCgYIKoZIzj0EAwIw
FzEVMBMGA1UEAwwMMzEuNzYuMTExLjE5MB4XDTI2MTAyMTIxNDMyMVoXDTM2MTAx
ODIxNDMyMVowFzEVMBMGA1UEAwwMMzEuNzYuMTExLjE5MFkwEwYHKoZIzj0CAQYI
KoZIzj0DAQcDQgAEueNwTJbtyRzFKkydp8aHhZw0fwpejUS4GFpYQ53Y1xcqbSzT
NPhVYrSAtFLaxiaoaj+xSd0doEWqUc3BWA0WGqN0MHIwHQYDVR0OBBYEFOAS/j6x
6P1Uh69TdQ0QGlcQaF33MB8GA1UdIwQYMBaAFOAS/j6x6P1Uh69TdQ0QGlcQaF33
MA8GA1UdEQQIMAaHBB9MbxMwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AoQwCgYIKoZIzj0EAwIDSAAwRQIhAJXCvaFqJBnuznsyl9YWZK7nfcGU47ioIcxu
pdlF8DbWAiBZt24VeVQRW3P/SJpV1z/k24wfYWp+BPg8Awz8GG9r1g==
-----END CERTIFICATE-----
)";

} // namespace Whitegram::Backend
