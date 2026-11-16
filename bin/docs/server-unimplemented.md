<!-- Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md. -->
# GS2Emu Unimplemented Features

## Classic era
###### 1.x

Support for client versions below 1.38 is not supported and may not work.

Clients 1.38 - 1.411 are fully supported with no known issues.

## Newmain era
###### 2.x / 3.x

Client authoritative:
- Full support with official with no known limitations.

Server authoritative (NPC-server):
- Trial accounts remain limited: guests map to script `"trial"` / `players[].trial`, but save-less trial accounts and `limitfreeplayers2` bomb/arrow limits are not fully stock-equivalent.
- Ghost mode gameplay is implemented for `ghostmodeenabled` / `ghostmodefornotstaff` (appearance, icon, mode packets). Fine client-side collision/walk-through behaviour still depends on the client honouring those packets.
- Translations for NPC messages in GS1 scripts are not supported.
- Lacking some script commands / features that may or may not have been implemented on official servers (lack of documentation to make full judgement).
- No support for the MUD commands used in Kingdoms.

## Modern era
###### 4.x - 6.037

Clientside GS1 only works up to and including the 5.007 client.

Clientside GS2 scripts do not work on 4.x clients.
There are opcode differences that still need to be figured out.

Clientside GS2 on the 5.007 client works and there is no known limitations.

Serverside GS2 scripts are not supported.

Translations using the modern PO (Portable Object) file format are not supported.
