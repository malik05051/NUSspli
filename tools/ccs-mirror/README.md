# ccs-mirror

A mirror of `ccs.wup.shop.nintendo.net` for the Wii U **Download Management**.
It answers from the public CDN that NUSspli itself downloads from.

## Why

NUSspli can hand a title to the Download Management of the Wii U (X in the
predownload menu). The system then downloads the title files from Nintendo's
secure content server `https://ccs.wup.shop.nintendo.net/ccs/download/<title ID>/<file>`.

This server answers the same paths from the public CDN
`http://ccs.cdn.wup.shop.nintendo.net/ccs/download/...` that NUSspli itself
downloads from. The console's DNS points `ccs.wup.shop.nintendo.net` at it, and
every other Nintendo server stays untouched.

It also logs what the console asks for, which the secure server never shows.
That log found the cause of **105-0955** (`BOSS_HTTP_ERROR_404_NOT_FOUND`) for
the update of Wii Sports Club. The console asked for `tmd.0`, a version that
does not exist, because NUSspli left the version in the task config at 0. Since
that fix the mirror is a fallback for a file the secure server really lost, and
a way to see what the Download Management fetches.

## The icon of a title: 105-1017

Before the Download Management takes a title at all it fetches the icon of it
from `https://idbe-wup.cdn.nintendo.net/icondata/<2 hex>/<title ID>.idbe`
(see [nn_idbe.rpl](https://wiiubrew.org/wiki/Nn_idbe.rpl)). A title whose icon
it does not get fails with **105-1017** and nothing is downloaded, no matter
what the ticket situation is: the icon is fetched first.

### Look at the console before you look at the server

**A missing icon is rarely the cause.** Nintendo still serves that host today:
it answers on TLS 1.0 with a certificate of its own CA, the one every Wii U
trusts, and the icon of a title comes down from it in a browser.

What fails is the console asking somewhere else. A Pretendo install patches
the icon server out of nim-boss - `src/iosu_url_patches.h` of
[Inkay](https://github.com/PretendoNetwork/Inkay) writes
`idbe-wup.cdn.pretendo.cc` over it - and that server has no icons, so **every**
handoff ends in 105-1017 while Nintendo has the icon all along. A 5.5.5E
console showed exactly that: a paid title and a demo both refused, and both
went through once the plugin was out of the way. The content server (`ccs`) is
not among the patched URLs, so only the icon is affected.

Turning the plugin off for the handoff is the whole fix there. The two options
below are for a title whose icon really is gone, which is a case that has not
been seen yet.

### An icon out of a folder

`--idbe-dir` names a folder of `.idbe` files that answer where the servers do
not:

```
--idbe-dir /etc/ccs-mirror/icons
```

A file is looked up under the name the console asked for, so the icon of
`0005000010101D00` is `0005000010101D00.idbe` in that folder, and a console
asking for a version wants `0005000010101D00-16.idbe`. The upstream wins
whenever it answers, so the folder only fills the holes.

Where such a file comes from is the catch: any `.idbe` of that title, from a
console that still has it or from an archive. For a title Nintendo dropped
there may be nowhere left to get one, which is what the next part is for.

### An icon made here

`--idbe-generate` makes the icon instead of looking for it:

```
--idbe-generate
```

The format is written down - the first byte is zero, the second picks one of
four AES-128 keys, the rest is CBC with a fixed IV, and the decrypted data
holds a SHA-256 of itself, the title ID, a magic, the names and a TGA - so
`idbe.py` builds exactly that for the title the console asked for: its own
title ID and version, its title ID as the name and a plain grey 128 by 128
square as the picture. It needs nothing installed, the AES is in the file.

An icon from `--idbe-dir` wins over one made here, and both only answer where
Nintendo does not. `python3 idbe.py` checks the AES against the vector of
FIPS-197 and the layout against the format.

**What this cannot say** is whether the console is happy with it. The icon
carries the right title ID and a hash over itself that matches, so everything
that can be checked here is right, but it was never handed to a console: that
is the one thing left to find out. Wherever the system shows the picture it
will be a grey square.

## What it does not do

It only passes on what the public CDN gives to anyone:
- the title files (encrypted, as on the CDN);
- the ticket (`cetk`) of free content such as updates.

A title you have to buy gets no ticket from here: the CDN answers 404 for it,
just like before. The Download Management installs with the ticket the console
has, which for a title nobody bought is the one NUSspli puts there itself (X in
the predownload menu). The mirror only fixes files that went missing from the
secure server.

## How the console trusts it

The console checks the server certificate against Nintendo's CAs. Firmware
**5.5.5 and newer** accepts a certificate forged with Pretendo's
[SSSL](https://github.com/PretendoNetwork/SSSL) from the console's own
"Nintendo CA - G3" certificate. This needs no homebrew on the console for
this part.

Two catches:
- **It may not work for the download service.** SSSL works for system services
  that use the console's SSL module. Apps with their own SSL stack (the eShop,
  WebKit apps) are not covered. Whether the download service is covered can
  only be found out on a console: see **Testing**.
- **Old TLS only.** The console speaks TLS 1.0/1.1 with old ciphers. The mirror
  turns them on explicitly (OpenSSL security level 0), because current Ubuntu
  refuses them by default.

## Setup

You need:
- the Wii U on 5.5.5 or newer, with Aroma and its FTP plugin (ftpiiu) for step 1;
- a Linux PC on the same network as the Wii U (for SSSL and the DNS);
- a Google Cloud VM with Ubuntu, Python 3 and a public IP.

### 1. Copy the Nintendo CA - G3 certificate off the console

Over FTP (ftpiiu), copy this file to your PC:

```
/storage_mlc/sys/title/0005001b/10054000/content/scerts/CACERT_NINTENDO_CA_G3.der
```

This is a public CA certificate, not a secret of your console.

### 2. Forge the certificate with SSSL (on the PC)

With Docker:

```bash
git clone https://github.com/PretendoNetwork/SSSL.git
cd SSSL
cp /path/to/CACERT_NINTENDO_CA_G3.der .
docker build . -t sssl
docker run -it --rm -v .:/app/certs sssl -g3 /app/certs/CACERT_NINTENDO_CA_G3.der -o /app/certs
```

Or with Node.js: `npm i`, then `node patch -g3 ./CACERT_NINTENDO_CA_G3.der -o ./`.

The mirror needs two of the outputs:
- `cert-chain.pem`: the site certificate followed by the forged CA;
- `ssl-cert-private-key.pem`: its private key.

**Keep `forged-ca-private-key.pem` private and do not upload it anywhere.**
Anyone with it can make certificates that every Wii U on 5.5.5+ accepts for
any server. The mirror does not need it.

### 3. Install the mirror on the VM

From the PC (replace `VM` and `ZONE` with your instance name and zone):

```bash
gcloud compute scp --zone ZONE \
    ccs_mirror.py idbe.py ccs-mirror.service \
    /path/to/SSSL/cert-chain.pem /path/to/SSSL/ssl-cert-private-key.pem \
    VM:~
```

Then on the VM (`gcloud compute ssh VM --zone ZONE`):

```bash
sudo apt-get update && sudo apt-get install -y python3
sudo install -d -m 755 /opt/ccs-mirror
sudo install -d -m 700 /etc/ccs-mirror
sudo install -m 644 ccs_mirror.py idbe.py /opt/ccs-mirror/
sudo install -m 600 cert-chain.pem ssl-cert-private-key.pem /etc/ccs-mirror/
sudo install -m 644 ccs-mirror.service /etc/systemd/system/
rm ccs_mirror.py idbe.py ccs-mirror.service cert-chain.pem ssl-cert-private-key.pem
```

Allow only your home. Find your public IP on the PC with
`curl -4 https://ifconfig.me`, then on the VM:

```bash
echo 'CCS_MIRROR_ALLOW=203.0.113.7' | sudo tee /etc/ccs-mirror/env   # your IP
sudo systemctl daemon-reload
sudo systemctl enable --now ccs-mirror
sudo journalctl -u ccs-mirror -f      # "Listening on 0.0.0.0:443"
```

### 4. Open port 443 for your home only (on the PC, gcloud)

```bash
gcloud compute firewall-rules create ccs-mirror-https \
    --direction INGRESS --allow tcp:443 \
    --source-ranges 203.0.113.7/32 --target-tags ccs-mirror
gcloud compute instances add-tags VM --zone ZONE --tags ccs-mirror
```

Use your own IP, the same one as in `/etc/ccs-mirror/env`. If your home IP
changes, update both.

Find the VM's public IP with
`gcloud compute instances describe VM --zone ZONE --format='get(networkInterfaces[0].accessConfigs[0].natIP)'`.
Reserving it as a static IP keeps it from changing when the VM restarts.

To check from the PC that it answers over TLS 1.0:

```bash
openssl s_client -connect VM_IP:443 -tls1 -cipher 'DEFAULT:@SECLEVEL=0' </dev/null | grep -E "Protocol|Cipher is"
```

### 5. Point only this server at the VM (DNS on the PC)

Run dnsmasq on the PC as before, with one `--address` line added. Use the
PC's LAN IP from `hostname -I` and the VM's public IP:

```bash
sudo dnsmasq --no-daemon --log-queries --no-resolv --server=1.1.1.1 \
     --listen-address=PC_LAN_IP --bind-interfaces \
     --address=/ccs.wup.shop.nintendo.net/VM_IP
```

On the Wii U, set **Proxy: Don't Set** and **DNS** manually to the PC's LAN IP.
Every other name is answered normally.

## Testing

1. Watch the mirror: `sudo journalctl -u ccs-mirror -f` on the VM.
2. On the Wii U, in NUSspli, open the title (for example the Wii Sports Club
   update) and press **X**, then **A**.
3. Read the log:
   - `GET /ccs/download/<title ID>/... -> 200`: the console trusts the mirror
     and takes the files from it. The Download Management should get past
     105-0955 now.
   - `... -> 404 (upstream)`: the public CDN does not have that file either.
   - `... -> 404 (unknown path)`: the console asks for a path the mirror does
     not know yet. Send that line.
   - No line at all, and dnsmasq shows the query for
     `ccs.wup.shop.nintendo.net` answered with the VM IP: the console did not
     accept the certificate. The download service is one of the parts SSSL
     does not cover, and this route ends here.

## Afterwards

- On the Wii U, set **DNS back to automatic**.
- `sudo systemctl disable --now ccs-mirror` on the VM when you no longer need
  it, and delete the firewall rule:
  `gcloud compute firewall-rules delete ccs-mirror-https`.

## Options

```
ccs_mirror.py --cert CERT --key KEY [--bind 0.0.0.0] [--port 443]
              [--allow IP[,NET...]] [--timeout 60]
```

`--allow` takes IPs and networks (`203.0.113.7`, `198.51.100.0/24`).
Everyone else gets 403. The mirror only answers
`/ccs/download/<16 hex digits>/<tmd | tmd.N | cetk | content ID[.h3]>`, with
`GET` and `HEAD`, and passes `Range` on, so interrupted downloads resume.
