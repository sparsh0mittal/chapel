use BlockDist;

var Dist = new dist(new Block(boundingBox=[1..9]));

var D1 = Dist.newArithmeticDom(1, int(32), false);
var D2 = Dist.newArithmeticDom(1, int(32), false);
D1.dsiSetIndices([1..9]);
D2.dsiSetIndices([2..10]);

forall (i,j) in (D1, D2) do
  writeln("on ", here.id, ", (i,j) is: ", (i,j));
