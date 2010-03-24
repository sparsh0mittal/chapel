use BlockDist, CyclicDist, BlockCycDist;

config param distString: string = 'default';

config const n1 = 100;
config const n2 = 20;
config const n3 = 5;
config const n4 = 3;
config const n5 = max(int(64));
config const o5 = 8;

const Space1 = [1..n1];
const Space2 = [1..n2, 1..n2];
const Space3 = [1..n3, 1..n3, 1..n3];
const Space4 = [1..n4, 1..n4, 1..n4, 1..n4];
const Space2D64 = [n5-o5..n5, n5-o5..n5];

def setupDistributions() {
  if distString == 'default' {
    return (
            new dist(new DefaultDist()),
            new dist(new DefaultDist()),
            new dist(new DefaultDist()),
            new dist(new DefaultDist()),
            new dist(new DefaultDist())
           );
  }
  if distString == 'block' {
    return (
            new dist(new Block(rank=1, boundingBox=Space1)),
            new dist(new Block(rank=2, boundingBox=Space2)),
            new dist(new Block(rank=3, boundingBox=Space3)),
            new dist(new Block(rank=4, boundingBox=Space4)),
            new dist(new Block(rank=2, idxType=int(64), boundingBox=Space2D64))
           );
  }
  if distString == 'cyclic' {
    return (
            new dist(new Cyclic(rank=1, idxType=int)),
            new dist(new Cyclic(rank=2, idxType=int)),
            new dist(new Cyclic(rank=3, idxType=int)),
            new dist(new Cyclic(rank=4, idxType=int)),
            new dist(new Cyclic(rank=2, idxType=int(64)))
           );
  }
  if distString == 'blockcyclic' {
    return (
            new dist(new BlockCyclic(rank=1, idxType=int, low=tuple(0), blk=tuple(3))),
            new dist(new BlockCyclic(rank=2, idxType=int, low=(0,0), blk=(3,3))),
            new dist(new BlockCyclic(rank=3, idxType=int, low=(0,0,0), blk=(3,3,3))),
            new dist(new BlockCyclic(rank=4, idxType=int, low=(0,0,0,0), blk=(3,3,3,3))),
            new dist(new BlockCyclic(rank=2, idxType=int(64), low=(0:int(64),0:int(64)), blk=(2,3)))
           );
  }
}

const (Dist1D, Dist2D, Dist3D, Dist4D, Dist2D64) = setupDistributions();

//
// creates a tuple of size 'rank' initialized with values 'x'
//
def fill(param rank, x) {
  if rank == 1 {
    var y: 1*x.type;
    y(1) = x;
    return y;
  } else if rank == 2 {
    return (x, x);
  } else {
    return (x, (...fill(rank-1, x)));
  }
}

//
// creates a domain of rank 'rank' with each dimension equal to 'extent'
//
def rankDomain(param rank, extent) {
  return [(...fill(rank, extent))];
}

//
// returns a next integer starting at 1
//
var next_i = 0;
def next() { next_i += 1; return next_i; }
