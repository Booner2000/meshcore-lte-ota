#pragma once
// Insert the PEM root CA for your own HTTPS server before building.
namespace lte {
static const char RootCa[] = R"CERT(REPLACE_WITH_PEM_ROOT_CA)CERT";
}
