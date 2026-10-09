# Configuring a node over Bluetooth

For a node that has no Ethernet cable, or no address yet, a phone can set it up over Bluetooth Low Energy: the node's name, its Wi-Fi station
(the network of a router or an access point), a fixed address, the broker, the access point, the radars and the pins. It is the **same
configuration** as the web panel's, with the same users and the same set-up code. The ARMOR app for Android (ARMOR-ANDROID-CONTROL) is the client.
**None of this has yet been recorded between a board and a phone.**

## When a node listens

The setting *Bluetooth* (Network page of the panel, `ble.mode` in the settings) has three values:

| Value | The node advertises |
|---|---|
| `setup` (the default) | while it has no user, that is, until its first administrator is created, and also while a node set up to join a Wi-Fi network has no address yet (it stops two minutes after it gets one) |
| `always` | all the time; every operation but `hello` still needs a login |
| `off` | never; the Bluetooth stack is not even started |

A node that listens is called `ARMOR-` and the last six digits of its MAC (in capitals), and lists the service below in its advertisement, so a phone finds
it by name or by service.

## Security

* Writing needs an **encrypted link**: LE Secure Connections, pairing "just works", no bonding is kept. The node asks the phone to pair as soon as it
  connects, so Android shows its pairing dialog once per connection. This stops someone listening to the air. It does **not** stop someone who is present
  while the phone pairs (an active man in the middle), because "just works" has no way to check who is on the other end.
* On top of the link, every operation except `hello` needs the **set-up code** (a node with no user) or a **login** (a node with users), and changing
  anything needs an administrator. The set-up code is the one the node shows on its USB console, or the one derived from the fleet secret and the node's
  MAC (`tools/adopt_node.py` computes it, and the app can when it is given the secret).
* Five wrong passwords or codes lock the channel for 30 seconds, doubling up to five minutes.
* One phone at a time.

## GATT

| | UUID | Properties |
|---|---|---|
| Service | `a5c0de00-5261-4d4f-8000-41524d4f5200` | |
| RX (the app writes requests) | `a5c0de01-5261-4d4f-8000-41524d4f5200` | write, write without response; needs encryption |
| TX (the node sends answers) | `a5c0de02-5261-4d4f-8000-41524d4f5200` | notify (subscribe before the first request) |

The node asks for an MTU of 247; the app should request 247 or more as well. Every write and notification carries at most `MTU - 3` bytes.

## Framing

A message is a JSON object in UTF-8. On the wire it is `[length: 2 bytes, big-endian][the JSON]`, at most 6000 bytes, and the byte stream of a direction
may be cut anywhere: the receiver collects bytes until the length is reached. Two messages may follow each other in one write. A length of zero, or above
6000, discards what was collected (the app resends). The same framing is used both ways.

## Requests and answers

    request : {"id": 7, "op": "config.get", "args": { ... }}      id: a whole number, echoed in the answer; op: below; args: optional object
    answer  : {"id": 7, "ok": true,  "data": { ... }}
    error   : {"id": 7, "ok": false, "error": "code", "data": { ... }}     (data only for some errors)

Errors: `bad_request` (id 0: what arrived was not a request), `unknown_op`, `setup_required` (the node has no user: use `setup`), `unauthorized`
(log in), `forbidden` (needs an administrator, or `setup` on a node that has users), `wrong_code`, `wrong_credentials`, `too_many_attempts`
(`data.wait_s`), `invalid_name`, `weak_password`, `invalid` (`data.problems` lists `{"path","code"}` of the fields that are wrong), `storage`,
`wifi_busy`, `scan_failed`, `wifi_unavailable`.

## Operations

| op | Needs | args | data of the answer |
|---|---|---|---|
| `hello` | nothing | | `kind` (`radar`, `solar` or `electrical`), `node_id`, `name`, `mac`, `firmware`, `setup` (true when the node has no user), `layout`, `has_ip`, `ip`, `sta_connected`, `sta_ssid`, `sta_error` (why the Wi-Fi station is not connected: `network_not_found`, `wrong_password` or `failed`; empty when it is or has not tried), `ap_active` |
| `setup` | a node with no user | `code`, `user`, `password` | `{"restart_required":true}`; the connection is now signed in as that administrator, and the node keeps its set-up state until a `reboot` |
| `login` | a node with users | `user`, `password` | `{"role":"admin"}` or `"viewer"` |
| `logout` | | | |
| `config.get` | login | | `{"config": {...settings...}, "channel_auto": n, "firmware": "x.y.z"}`; passwords are never sent, only `password_set` flags |
| `config.put` | administrator | `config`: a full or **partial** settings document | `{"restart_required": bool}`, or the error `invalid` with `data.problems` |
| `status` | login | | the same as the panel's Overview (network, broker, radars, light, memory) |
| `wifi.scan` | administrator | | `{"networks":[{"ssid","rssi","channel","security"}]}`, strongest first; takes a few seconds; the node's own access point may drop its clients for a moment |
| `reboot` | administrator | | `{}`; the node restarts in three seconds |

The settings document is the one of the panel (`docs/NODE_PANEL.md`): sections `node`, `uplink`, `ip`, `ap`, `sta`, `mqtt`, `radars`, `sensors`, `pins`,
`ble`, `ui`. A partial document changes only what it names; a missing or empty password keeps the stored one (`"password_clear": true` erases it). To make a node
without a cable join a router's Wi-Fi, an app sends, after `login` or `setup`:

    {"id": 9, "op": "config.put", "args": {"config": {
        "uplink": "wifi", "sta": {"enabled": true, "ssid": "HomeRouter", "password": "the router password"},
        "node": {"name": "North fence 3"}, "mqtt": {"enabled": true, "uri": "mqtt://192.168.0.180:18883", "username": "field-node-x", "password": "..."}}}}

and then `reboot`. `wifi.scan` gives the app the list of networks to choose from.

## A first setup from a phone

1. Scan for the service (or names `ARMOR-XXXXXX`), connect, request the MTU, discover the service and enable notifications on TX. Android pairs when asked.
2. `hello`: if `setup` is true, send `setup` with the code, the administrator's name and a password (the connection is then signed in as that administrator); otherwise `login`.
3. On the same connection: `wifi.scan`, then `config.put` as above, then `reboot`. A node in mode `setup` stops advertising after the restart, because it has a user
   by then; put `ble.mode` to `always` in the same `config.put` if the phone must be able to reach it again.

## Limits of this work

* The radio side (NimBLE) builds, and the framing, the operations and their access rules are tested on a computer (`tests/test_node.cpp`); the node runs on real boards,
  but no phone has yet been recorded talking to its Bluetooth channel.
* The "just works" pairing is the weakest part; a passkey pairing would need the node to show the passkey (it has no display), so it is not offered.
* The set-up code and the passwords are sent inside the encrypted link; a phone that pairs with an impostor node would send them to it.
