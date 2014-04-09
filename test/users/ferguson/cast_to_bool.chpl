proc foo(x:uint, y:uint)
{
  writeln((x & y):bool);
}

writeln((0x3 & 0x1):bool);
foo(0x3, 0x1);
writeln((0x300 & 0x100):bool);
foo(0x300, 0x100);
writeln((0x30000 & 0x10000):bool);
foo(0x30000, 0x10000);
writeln((0x3000000 & 0x1000000):bool);
foo(0x3000000, 0x1000000);
writeln((0x300000000 & 0x100000000):bool);
foo(0x300000000, 0x100000000);
writeln((0x30000000000 & 0x10000000000):bool);
foo(0x30000000000, 0x10000000000);
writeln((0x3000000000000 & 0x1000000000000):bool);
foo(0x3000000000000, 0x1000000000000);

proc foo8(x:uint, y:uint)
{
  writeln((x & y):bool(8));
}


writeln((0x3 & 0x1):bool(8));
foo8(0x3, 0x1);
writeln((0x300 & 0x100):bool(8));
foo8(0x300, 0x100);
writeln((0x30000 & 0x10000):bool(8));
foo8(0x30000, 0x10000);
writeln((0x3000000 & 0x1000000):bool(8));
foo8(0x3000000, 0x1000000);
writeln((0x300000000 & 0x100000000):bool(8));
foo8(0x300000000, 0x100000000);
writeln((0x30000000000 & 0x10000000000):bool(8));
foo8(0x30000000000, 0x10000000000);
writeln((0x3000000000000 & 0x1000000000000):bool(8));
foo8(0x3000000000000, 0x1000000000000);

proc foo16(x:uint, y:uint)
{
  writeln((x & y):bool(16));
}


writeln((0x3 & 0x1):bool(16));
foo16(0x3, 0x1);
writeln((0x300 & 0x100):bool(16));
foo16(0x300, 0x100);
writeln((0x30000 & 0x10000):bool(16));
foo16(0x30000, 0x10000);
writeln((0x3000000 & 0x1000000):bool(16));
foo16(0x3000000, 0x1000000);
writeln((0x300000000 & 0x100000000):bool(16));
foo16(0x300000000, 0x100000000);
writeln((0x30000000000 & 0x10000000000):bool(16));
foo16(0x30000000000, 0x10000000000);
writeln((0x3000000000000 & 0x1000000000000):bool(16));
foo16(0x3000000000000, 0x1000000000000);


proc foo32(x:uint, y:uint)
{
  writeln((x & y):bool(32));
}


writeln((0x3 & 0x1):bool(32));
foo32(0x3, 0x1);
writeln((0x300 & 0x100):bool(32));
foo32(0x300, 0x100);
writeln((0x30000 & 0x10000):bool(32));
foo32(0x30000, 0x10000);
writeln((0x3000000 & 0x1000000):bool(32));
foo32(0x3000000, 0x1000000);
writeln((0x300000000 & 0x100000000):bool(32));
foo32(0x300000000, 0x100000000);
writeln((0x30000000000 & 0x10000000000):bool(32));
foo32(0x30000000000, 0x10000000000);
writeln((0x3000000000000 & 0x1000000000000):bool(32));
foo32(0x3000000000000, 0x1000000000000);

proc foo64(x:uint, y:uint)
{
  writeln((x & y):bool(64));
}


writeln((0x3 & 0x1):bool(64));
foo64(0x3, 0x1);
writeln((0x300 & 0x100):bool(64));
foo64(0x300, 0x100);
writeln((0x30000 & 0x10000):bool(64));
foo64(0x30000, 0x10000);
writeln((0x3000000 & 0x1000000):bool(64));
foo64(0x3000000, 0x1000000);
writeln((0x300000000 & 0x100000000):bool(64));
foo64(0x300000000, 0x100000000);
writeln((0x30000000000 & 0x10000000000):bool(64));
foo64(0x30000000000, 0x10000000000);
writeln((0x3000000000000 & 0x1000000000000):bool(64));
foo64(0x3000000000000, 0x1000000000000);

