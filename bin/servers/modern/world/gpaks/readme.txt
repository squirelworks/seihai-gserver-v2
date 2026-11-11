Seihai lab: resource packages for this server folder.

Put .gpak and .gpake files here and list the ones to send at login in
config/serveroptions.txt (gpaks = art.gpake, more.gpak). Build them with
`seihai-servertools gpak pack`, which also writes each .gpake's key to
config/gpakkeys.txt. Keep that keys file in config/, never in world/.

.gpake (encrypted) goes to 4.20r8 and every 5.x and 6.x client.
.gpak (plain) goes to 5.07 and later only: 4.20r8 saves it but never loads it.
