#pragma once

#include "mtproto/details/mtproto_bypass_hosts.h"

namespace AyuFeatures::BypassHosts {

void init();
void syncFromSettings();
void setServerMirrors(const std::vector<QString> &mirrors);
void setServerMirrorHosts(const std::vector<QString> &hosts);

} // namespace AyuFeatures::BypassHosts
