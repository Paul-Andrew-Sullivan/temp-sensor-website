// Copy this file to secrets.h and fill it in. secrets.h is ignored by git
// because the repo is public.

// The Wi-Fi network with internet access the box joins so it can send mail.
// A phone hotspot works. Leave the password "" for an open network.
#define WIFI_SSID     "your-hotspot-name"
#define WIFI_PASSWORD "your-hotspot-password"

// The Gmail account the box sends FROM. Make a throwaway account, turn on
// 2-step verification, then Google Account > Security > App passwords and
// create one. That 16-character code goes here, not the account password.
#define MAIL_ADDRESS      "thermobox.example@gmail.com"
#define MAIL_APP_PASSWORD "abcd efgh ijkl mnop"

// Where the box reports to, and the token the server checks. PROBE_TOKEN is
// the line of the same name in the server's .env.
#define INGEST_URL  "https://thermobox.paulandrewsullivan.com/ingest"
#define PROBE_TOKEN "the-probe-token-from-the-server-env"
