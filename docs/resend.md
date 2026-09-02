# Resend setup for alerts

The backend sends alert email through the Resend HTTP API (`POST https://api.resend.com/emails`). Nothing else is needed on the server side.

1. Create a Resend account at https://resend.com and add the domain `paulandrewsullivan.com` under Domains. Resend shows two or three DNS records (DKIM TXT, SPF/MX for the bounce subdomain). Add them in the Cloudflare DNS panel for the zone and wait for Resend to show the domain as verified.
2. Create an API key (Sending access is enough).
3. On the Unraid box edit `/mnt/user/appdata/thermo/.env`:

   ```
   RESEND_API_KEY=re_xxxxxxxxx
   ALERT_FROM=Thermometer <thermo@paulandrewsullivan.com>
   ```

4. `docker restart thermo-api`.

Until the domain is verified, Resend only lets you send from `onboarding@resend.dev` to the account owner's own address. That is enough for a first test: set `ALERT_FROM=onboarding@resend.dev` and put your own email in the page's "Send to" field.

Text messages: put the phone's carrier gateway address in "Send to", for example `3195551234@txt.att.net`, `3195551234@vtext.com` (Verizon), `3195551234@tmomail.net` (T-Mobile). Carriers deliver these as SMS, usually within a minute.

Test without hardware: turn demo on, pick the `high` scenario, set the maximum to 40. Sensor 1 climbs past it within a couple of minutes and the page's "Last message sent" line updates. Delivery failures are logged by `docker logs thermo-api`.
