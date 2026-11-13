Default fonts for 5.x and 6.x clients.

5.24r4 and 6.03r7 bring no fonts of their own. They ask the server for
arial.ttf, arialbd.ttf, tempsitc.ttf and tempsitcbd.ttf, and with none they
draw no text at all: no NPC message, no chat, no HUD letters. These files are
the Liberation Sans fonts (SIL Open Font License 1.1, see
LICENSE-Liberation.txt; Liberation Sans is metric-compatible with Arial),
unmodified and only saved under the names the clients ask for. The two
tempsitc files stand in for Tempus Sans ITC, which has no free metric twin.
Replace them with any TrueType fonts you have the right to serve.
