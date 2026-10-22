#pragma once
#include <advanced_statistics.hpp>
class WebServer;
void local_settings_register(WebServer& server);
void local_settings_process(const advanced::Counters& total);

bool local_settings_blocks_checkpoint();
