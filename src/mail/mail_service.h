#pragma once
#include <mail_core.hpp>
#ifdef ADVANCED_MAIL
class WebServer;
bool mail_config_load(mail::Config& config);
bool mail_config_save(const mail::Config& config);
void mail_publish(const advanced::Snapshot& snapshot);
bool mail_start();
bool mail_claim_settings();
void mail_release_settings();
bool mail_clear_history();
bool mail_queue_test();
bool mail_queue_probe();
const char* mail_status();
#endif
