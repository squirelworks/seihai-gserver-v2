# Writes the two npcdemo levels. Run from anywhere: python3 tools/lab/mknpcdemo.py
# The board is the stock onlinestartlocal.nw board; the NPC scripts are below.
import os
root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
src = os.path.join(root, 'bin/servers/default/world/onlinestartlocal.nw')
dst = os.path.join(root, 'bin/servers/npcdemo/world/')
board = [l for l in open(src).read().splitlines() if l.startswith('BOARD ')]
assert len(board) == 64

def level(name, npcs, links=(), signs=()):
    out = ['GLEVNW01'] + board
    for l in links: out.append('LINK ' + l)
    for (x, y, text) in signs: out += [f'SIGN {x} {y}', text, 'SIGNEND']
    for (img, x, y, code) in npcs:
        out.append(f'NPC {img} {x} {y}')
        out += code.strip('\n').splitlines()
        out.append('NPCEND')
    open(dst + name, 'w', newline='\n').write('\n'.join(out) + '\n')

start = [
('-', 30, 30, r'''
// 1. Greeter: created, playerenters, timeout, setcharprop, showcharacter.
if (created) {
  showcharacter;
  setcharprop #n,Greeter;
  setcharprop #3,head0.png;
  dir = 2;
  this.ticks = 0;
  timeout = 1;
}
if (playerenters) {
  setcharprop #c,Welcome #n! The NPC-server is running.;
}
if (timeout) {
  this.ticks = this.ticks + 1;
  if (this.ticks % 10 == 0) {
    setcharprop #c,Awake for #v(this.ticks) seconds;
  }
  timeout = 1;
}
'''),
('block.png', 20, 36, r'''
// 2. Sign: playertouchsme, say2.
if (created) setshape 1,32,32;
if (playertouchsme) {
  say2 This text comes from#bthe built-in NPC-server.;
}
'''),
('block.png', 40, 36, r'''
// 3. Counter: this. variables, server. flags, setstring, message.
if (created) setshape 1,32,32;
if (playertouchsme) {
  this.count = this.count + 1;
  set server.npcdemo.touched;
  setstring server.npcdemo.last,#a;
  message Touched #v(this.count) times;
}
'''),
('block.png', 30, 50, r'''
// 4. Warp: setlevel2.
if (created) setshape 1,32,32;
if (playertouchsme) {
  setlevel2 npcdemo_room.nw,30,40;
}
'''),
('block.png', 45, 45, r'''
// 5. Weapon giver: addweapon, hasweapon.
if (created) setshape 1,32,32;
if (playertouchsme) {
  if (hasweapon(Demo Wand)) {
    message You already have it;
  } else {
    addweapon Demo Wand;
    message Got the Demo Wand;
  }
}
'''),
('-', 15, 45, r'''
// 6. Listener: playerchats, strequals, #c, #n, timevar.
if (created) {
  showcharacter;
  setcharprop #n,Listener;
}
if (playerchats && strequals(#c,hello)) {
  setcharprop #c,Hello #n, I heard you on the server.;
}
if (playerchats && strequals(#c,time)) {
  setcharprop #c,timevar is #v(timevar);
}
'''),
('block.png', 50, 30, r'''
// 7. Trigger target: triggeraction from the client, #p().
if (created) setshape 1,32,32;
if (actiondemo) {
  message Got demo with #p(0);
}
'''),
('block.png', 10, 30, r'''
// 8. Split script: the server half runs here, the client half is sent to the client.
if (created) setshape 1,32,32;
if (playertouchsme) {
  setplayerprop #c,The server half ran;
}
//#CLIENTSIDE
if (playerenters) {
  showtext 200,20,Arial,b,The client half ran;
}
'''),
('block.png', 50, 45, r'''
// 9. Spawner: putnpc2 with a script.
if (created) setshape 1,32,32;
if (playertouchsme) {
  putnpc2 52,45,{
    if (created) {
      setimg block.png;
      setshape 1,32,32;
      message I was put by putnpc2;
      timeout = 5;
    }
    if (timeout) destroy;
  };
}
'''),
('-', 30, 20, r'''
// 10. Walker: move with the movementfinished option.
if (created) {
  showcharacter;
  setcharprop #n,Walker;
  this.side = 0;
  timeout = 0.5;
}
if (timeout || movementfinished) {
  if (this.side == 0) {
    move 6,0,2,8;
    this.side = 1;
  } else {
    move -6,0,2,8;
    this.side = 0;
  }
}
'''),
('block.png', 20, 50, r'''
// 11. Blinker: hide, show, timeout.
if (created) timeout = 2;
if (timeout) {
  if (visible) hide;
  else show;
  timeout = 2;
}
'''),
('block.png', 10, 50, r'''
// 12. Strings and arrays: setstring, addstring, strlen, tokenize, #t.
if (created) setshape 1,32,32;
if (playertouchsme) {
  setstring this.list,;
  addstring this.list,apple;
  addstring this.list,pear;
  tokenize one two three;
  message List #v(strlen(#s(this.list))) chars, tokens #v(tokenscount) #t(2);
}
'''),
]
level('npcdemo_start.nw', start)

room = [
('block.png', 30, 50, r'''
// Back to the start level.
if (created) setshape 1,32,32;
if (playertouchsme) {
  setlevel2 npcdemo_start.nw,30,46;
}
'''),
('-', 30, 30, r'''
// Room keeper: playerenters, playerleaves, server. flags read back.
if (created) {
  showcharacter;
  setcharprop #n,Keeper;
}
if (playerenters) {
  if (server.npcdemo.touched) setcharprop #c,You touched the counter. Last: #s(server.npcdemo.last);
  else setcharprop #c,You have not touched the counter yet.;
}
if (playerleaves) {
  setcharprop #c,Bye;
}
'''),
]
level('npcdemo_room.nw', room)
