//
// Block-cyclic dimension specifier - for use with DimensionalDist2D.
//

use DimensionalDist2D;


config const BlockCyclicDim_allowParLeader = true;
config param BlockCyclicDim_enableArrayIterWarning = false;  // 'false' for testing

// the types to use for blockSzie and numLocales
type cycSizeT = uint(32);     // unsigned - for optimization
type cycSizeTuser = int;      // for versatility

class BlockCyclicDim {
  // distribution parameters
  const numLocales: cycSizeTuser;
  const lowIdx: int(64); // ensure we can always subtract it
  const blockSize: cycSizeTuser;

  // for debugging
  const name:string;

  // tell the compiler these are positive
  proc blockSizePos   return blockSize: cycSizeT;
  proc numLocalesPos  return numLocales: cycSizeT;
  const cycleSizePos: cycSizeT = blockSizePos * numLocalesPos;
}

class BlockCyclic1dom {
  type idxType;
  type stoIndexT;
  param stridable: bool;

  // for debugging
  const name:string;

  // convenience
  proc rangeT type  return range(idxType, BoundedRangeType.bounded, stridable);

  // our range, normalized; its absolute stride
  var wholeR: rangeT;
  var wholeRstrideAbs: idxType;

  // a copy of BlockCyclicDim constants
  const lowIdxAdj: idxType;
  const blockSizePos, numLocalesPos, cycleSizePos: cycSizeT;

  // amount of storage per locale per cycle - depends on wholeR.stride
  var storagePerCycle: cycSizeT = 0;
}

class BlockCyclic1locdom {
  type idxType;
  type stoIndexT;
  const locId: locIdT;
}


/////////// privatization - start

proc BlockCyclicDim.dsiSupportsPrivatization1d() param return true;

proc BlockCyclicDim.dsiGetPrivatizeData1d() {
  return (lowIdx, blockSize, numLocales, name);
}

proc BlockCyclicDim.dsiPrivatize1d(privatizeData) {
  return new BlockCyclicDim(lowIdx = privatizeData(1),
                   blockSize = privatizeData(2),
                   numLocales = privatizeData(3),
                   name = privatizeData(4));
}

proc BlockCyclicDim.dsiUsesLocalLocID1d() param return false;

proc BlockCyclic1dom.dsiSupportsPrivatization1d() param return true;

proc BlockCyclic1dom.dsiGetPrivatizeData1d() {
  return tuple(wholeR, wholeRstrideAbs, storagePerCycle, lowIdxAdj, name);
}

proc BlockCyclic1dom.dsiPrivatize1d(privDist, privatizeData) {
  assert(privDist.locale == here); // sanity check
  return new BlockCyclic1dom(idxType   = this.idxType,
                  stoIndexT = this.stoIndexT,
                  stridable = this.stridable,
                  name            = privatizeData(5),
                  wholeR          = privatizeData(1),
                  wholeRstrideAbs = privatizeData(2),
                  storagePerCycle = privatizeData(3),
                  lowIdxAdj       = privatizeData(4),
                  // could include these in privatizeData
                  blockSizePos  = privDist.blockSizePos,
                  numLocalesPos = privDist.numLocalesPos,
                  cycleSizePos  = privDist.cycleSizePos);
}

proc BlockCyclic1dom.dsiGetReprivatizeData1d() {
  return tuple(wholeR, wholeRstrideAbs, storagePerCycle);
}

proc BlockCyclic1dom.dsiReprivatize1d(other, reprivatizeData) {
  if other.idxType   != this.idxType ||
     other.stoIndexT != this.stoIndexT ||
     other.stridable != this.stridable then
    compilerError("inconsistent types in privatization");

  this.wholeR          = reprivatizeData(1);
  this.wholeRstrideAbs = reprivatizeData(2);
  this.storagePerCycle = reprivatizeData(3);
}

proc BlockCyclic1dom.dsiUsesLocalLocID1d() param return false;

proc BlockCyclic1dom.dsiLocalDescUsesPrivatizedGlobalDesc1d() param return false;

/////////// privatization - end


// Check all restrictions/assumptions that must be satisfied by the user
// when constructing a 1-d BlockCyclic distribution.
inline proc BlockCyclicDim.checkInvariants() {
  assert(blockSize > 0, "BlockCyclic1d-blockSize");
  assert(numLocales > 0, "BlockCyclic1d-numLocales");
}

proc BlockCyclicDim.toString()
  return "BlockCyclicDim(" + numLocales:string + ", " +
         lowIdx:string + ", " + blockSize:string + ")";

// Assert that the value of 'src' is preserved when casting it to 'destT'.
inline proc _checkFitsWithin(src: integral, type destT)
  where _isIntegralType(destT)
{
  inline proc ensure(arg:bool) { assert(arg); }
  type maxuT = uint(64); // the largest unsigned type
  type srcT = src.type;
  proc numMantBits(type T) param
    return numBits(T) - if _isSignedType(T) then 1 else 0;

  if _isUnsignedType(destT) {
    ensure(isNonnegative(src));
    if numMantBits(srcT) > numMantBits(destT) then
      ensure(src:maxuT <= max(destT):maxuT);

  } else {
    // destT is signed
    if _isUnsignedType(srcT) && numMantBits(srcT) > numMantBits(destT) then
      ensure(src:maxuT <= max(destT):maxuT);
    if _isSignedType(srcT) && numBits(destT) < numBits(srcT) {
      ensure(src <= max(destT):srcT);
      ensure(src >= min(destT):srcT);
    }
  }
}

proc BlockCyclicDim.dsiNewRectangularDom1d(type idxType, param stridable: bool,
                                  type stoIndexT)
{
  checkInvariants();
  _checkFitsWithin(this.lowIdx, idxType);
  const lowIdxDom = this.lowIdx: idxType;

  // Allow for idxType and/or stoIndexT to be unsigned, by replacing
  //  ind0 = ind -lowIdx  -->  ind0 = ind + lowIdxAdj
  //
  // where lowIdxAdj is
  //  -lowIdx whenever possible (more natural for debugging), otherwise
  //  -lowIdx shifted up by a multiple of cycleSize until it is >=0.
  //
  proc adjustLowIdx()  return cycleSizePos - mod(lowIdxDom, cycleSizePos);
  //
  const lowIdxAdj: idxType =
    if _isSignedType(idxType)
    then
      if _isSignedType(stoIndexT)
      then
        -lowIdxDom
      else
        if lowIdxDom <= 0 then -lowIdxDom else adjustLowIdx()
    else
      if lowIdxDom == 0 then lowIdxDom else adjustLowIdx()
    ;

    const negate = _isSignedType(idxType) && lowIdxDom <= 0;

  const result = new BlockCyclic1dom(idxType = idxType,
                  stoIndexT = stoIndexT,
                  stridable = stridable,
                  lowIdxAdj = lowIdxAdj,
                  blockSizePos  = this.blockSizePos,
                  numLocalesPos = this.numLocalesPos,
                  cycleSizePos  = this.cycleSizePos,
                  name = this.name);

  return result;
}

proc BlockCyclic1dom.dsiIsReplicated1d() param return false;

proc BlockCyclic1dom.dsiNewLocalDom1d(type stoIndexT, locId: locIdT) {
  const result = new BlockCyclic1locdom(idxType = this.idxType,
                             stoIndexT = stoIndexT,
                             locId = locId);
  return result;
}

proc BlockCyclic1dom.dsiBuildRectangularDom1d(DD,
                                   param stridable:bool,
                                   rangeArg: range(idxType,
                                                   BoundedRangeType.bounded,
                                                   stridable))
{
  // There does not seem to be any optimizations from merging the two calls.
  const result = DD.dsiNewRectangularDom1d(idxType, stridable, stoIndexT);
  result.dsiSetIndices1d(rangeArg);
  return result;
}

proc BlockCyclic1locdom.dsiBuildLocalDom1d(newGlobDD, locId: locIdT) {
  assert(locId == this.locId);
  // There does not seem to be any optimizations from merging the two calls.
  const newLocDD = newGlobDD.dsiNewLocalDom1d(this.stoIndexT, locId);
  const newStoRng = newLocDD.dsiSetLocalIndices1d(newGlobDD, locId);
  return (newLocDD, newStoRng);
}


/////////////////////////////////

/* The following comment:
- reviews the math for BlockCyclic, for a single dimension;
- motivates some functions used in the implementation;
- defines the mapping from user to storage index space.

*** Given:

distribution parameters:
 lowIdx
 blockSize >= 1
 numLocales >= 1

a range 'whole' with parameters (aligned):
 wLo
 wHi
 wSt

user's domain index (a member of 'whole'):
 i
 i0 = (i-lowIdx)  - "zero-based" index
 (the formulas assume 1:1 correspondence between i and i0, for brevity)

 note:  wLo <= i <= wHi
        advanced: i = wLo + iSt * |wSt| where iSt - a non-negative integer

*** Notation:
 floor(a,b) = floor((real)a/(real)b)
 a div b = { assert a>=0 && b>=0; return floor(a,b); }
 a mod b = { assert b >= 0; return a - b*floor(a,b); }
 "advanced" = "skip upon first reading"

*** Define the "cycle" of indices that starts at lowIdx, traverses
each locale 0..#numLocales, while traversing offsets 0..#blockSize
on each locale, then starts over:

 cycSize = blockSize * numLocales
 cycNo(i) = floor(i0,cycSize)
 cycOff(i) = i0 mod cycSize

 note:  cycNo(wLo) <= cycNo(i) <= cycNo(wHi)
        0 <= cycOff(i) < cycSize

 // the locale number that hosts 'i', aka locId or "block number" blkNum
 locNo(i) = cycOff(i) div blockSize

 note:  0 <= locNo(i) < numLocales

 // position of 'i' within the locale, aka "block offset" blkOff
 locOff(i) = cycOff(i) mod blockSize

 note: 0 <= locOff(i) < blockSize

 advanced property:
   If i1 and i2 are members of 'whole'
     and fall on the same cycle and on the same locale
     (i.e. cycNo and locNo are the same),
   then
     (locOff(i1) div |wSt|) vs. (locOff(i2) div |wSt|) are distinct
       IFF
     i1 vs. i2 are distinct.

 advanced proof:
   In general,
     i1 == lowIdx + cycNo(i1)*cycSize + locNo(i1)*blockSize + locOff(i1)
     i1 == wLo + i1St * |wSt| for some integer i1St
   then
     locOff(i1) div |wSt| == (i1A + i1St * |wSt|) div |wSt| == i1B + i1St
   where
     i1A = wLo - (lowIdx + cycNo(i1)*cycSize + locNo(i1)*blockSize)
     i1B = i1A div |wSt|

   likewise
     locOff(i2) div |wSt| = i2B + i2St

   Note i1B==i2B - because cycNo and locNo are the same for i1 and i2.
   Note i1==i2 IFF i1St==i2St - because of the definition of i1St, i2St.
   The property, then, follows.

*** Assign each index of 'whole' to "storage" on its locale as follows:

 // the pair (locNo(j), storageOff(j)) is unique for each integer j
 storageOff(i) = cycNo(i) * blockSize + locOff(i)

Advdanced: compress the storage based on the above advanced property,
which implies that:
 the pair (locNo(i), storageOff(i) div |wSt|) is unique
 for each 'i' - member of 'whole'.

 storageIdx(i) = cycNo(i) * storagePerCycle + (locOff(i) div |wSt|)

where storagePerCycle is determined to ensure uniqueness of storageIdx(i)

 storagePerCycle
   = 1 + max(locOff(i) div |wSt|) for any i s.t.
                                  whole.member(i)==true and locNo(i) is fixed
   approximated as: 1 + ( (max locOff(i) for any i) div |wSt| )
   = 1 + ((blockSize-1) div |wSt|)

*** Advanced: replacing mod with Chapel's %.
Calculate i0 using the following:

 i0 = i - lowIdx + cycSize * cycAdj
   choosing any fixed integer cycAdj >= ceil( (lowIdx-wLo), cycSize )

This guarantees i0>=0, but modifies cycNo(i) by cycAdj.
The latter is acceptable for our purposes.
Having i0>=0 ensures that (i0 mod cycSize) == (i0 % cycSize).

Additional consideration: for any given i, we want cycNo(i)
to stay the same throughout the life of a domain descriptor.
(This is so that our storage indices remain consistent - which is
useful to implement Chapel's preservation of array contents upon
domain assignments.)
This implies that the same cycAdj should accomodate wLo for any
domain bounds that can be assigned to our domain descriptor.
That may not be convenient in practice.
*/

inline proc BlockCyclic1dom._dsiInd0(ind: idxType): idxType
  return ind + lowIdxAdj;

inline proc BlockCyclic1dom._dsiCycNo(ind: idxType)
  return divfloor(_dsiInd0(ind), cycleSizePos): idxType;

inline proc BlockCyclic1dom._dsiCycOff(ind: idxType)
  return mod(_dsiInd0(ind), cycleSizePos): cycSizeT;

// "formula" in the name emphasizes no sanity checking
inline proc BlockCyclic1dom._dsiLocNo_formula(ind: idxType): locIdT {
  // keep in sync with BlockCyclicDim.dsiIndexToLocale1d()
  const ind0 = _dsiInd0(ind);
  return
    if isNonnegative(ind0) then ( (ind0/blockSizePos) % numLocalesPos ): locIdT
    else  mod(divfloor(ind0, blockSizePos), numLocalesPos): locIdT
    ;
}

inline proc BlockCyclic1dom._dsiLocOff(ind: idxType)
  return ( _dsiCycOff(ind) % blockSizePos ): stoIndexT;

// hoist some common code
inline proc BlockCyclic1dom._dsiStorageIdx2(cycNo, locOff)
  return cycNo * storagePerCycle + _divByStride(locOff);

// "formula" in the name implies no sanity checking
// in particular at the moment its type may not be stoIndexT
inline proc BlockCyclic1dom._dsiStorageIdx_formula(ind: idxType)
  return _dsiStorageIdx2(_dsiCycNo(ind), _dsiLocOff(ind));

inline proc BlockCyclic1dom._dsiStorageIdx(ind: idxType)
  return _dsiStorageIdx_formula(ind): stoIndexT;

// oblivious of 'wholeR'
inline proc BlockCyclic1dom._dsiIndicesOnCycLoc(cycNo: idxType, locNo: locIdT)
  : range(idxType)
{
  const startCycle = (cycNo * cycleSizePos): idxType - lowIdxAdj;
  const startLoc = startCycle:idxType + locNo:idxType * blockSizePos:idxType;
  return startLoc..#blockSizePos:idxType;
}

/////////////////////////////////

proc BlockCyclicDim.dsiIndexToLocale1d(ind): locIdT {
  // keep in sync with BlockCyclic1dom._dsiLocNo_formula
  const ind0 = ind - lowIdx;
  const locNo =
    if ind0 >= 0 then ( (ind0 / blockSize) % numLocales ): locIdT
    else  mod(divfloor(ind0, blockSizePos), numLocalesPos): locIdT
    ;

  assert(0 <= locNo && locNo < numLocales);
  // todo: the following assert should not be needed - it can be proven
  assert(locNo == mod(ind0, cycleSizePos) / blockSize);

  return locNo;
}

// allow uint(64) indices, but assert that they fit in int(64)
inline proc BlockCyclicDim.dsiIndexToLocale1d(ind: uint(64)): locIdT {
  type convT = int(64);
  assert(ind <= max(convT));
  return dsiIndexToLocale1d(ind:convT);
}

//var debugD1Shown = false;
//proc BlockCyclic1dom.debugIsD1() return name == "D1";

proc BlockCyclic1dom.dsiSetIndices1d(rangeArg: rangeT): void {
  // For now, require the user to provide unambiguous ranges only.
  // This requirement could potentially be avoided (as long as no arrays
  // are declared over the domain), but it simplifies/speeds up our code.
  //
  // todo: document this in the spec for this distribution.
  // see also an assert is dsiSetLocalIndices1d()
  assert(!rangeArg.isAmbiguous());

  const prevStoragePerCycle = storagePerCycle;

  // As of this writing, alignedLow/High are valid even for empty ranges
  if stridable {
    wholeR = rangeArg.alignedLow..rangeArg.alignedHigh by rangeArg.stride;
    wholeRstrideAbs = abs(rangeArg.stride): idxType;
    storagePerCycle = (1 + (blockSizePos - 1) / wholeRstrideAbs): cycSizeT;
  } else {
    assert(rangeArg.stride == 1); // ensures we can simplify things
    wholeR = rangeArg.alignedLow..rangeArg.alignedHigh;
    wholeRstrideAbs = 0; // be sure nobody ever reads this
    storagePerCycle = blockSizePos;
  }

//proc writei(i) {
//  write("(", _dsiCycNo(i), ",", _dsiLocNo_formula(i),
//        ",", _dsiCycOff(i) % blockSizePos, ")");
//}
//writeln();
//write  ("dsiSetIndices1d(", name, ", ", rangeArg, ") --> ");
//  write(wholeR, " | ");
//  writei(wholeR.low); write(" .. "); writei(wholeR.high);
//  writeln();
//if debugIsD1() then debugD1Shown = false;

  if prevStoragePerCycle != 0 && storagePerCycle != prevStoragePerCycle then
    stderr.writeln("warning: array resizing is not implemented upon change in dimension stride with 1-d BlockCyclic distribution");
}

inline proc BlockCyclic1dom._divByStride(locOff)  return
  if stridable then ( locOff / wholeRstrideAbs ): stoIndexT
  else              locOff: stoIndexT;

// _dsiStorageLow(), _dsiStorageHigh(): save a few mods and divisions
// at the cost of potentially allocating more storage
inline proc BlockCyclic1dom._dsiStorageLow(locId: locIdT): stoIndexT {
  const lowW = wholeR.low;

  // smallest cycNo(i) for i in wholeR
  var   lowCycNo  = _dsiCycNo(lowW): stoIndexT;
  const lowLocNo  = _dsiLocNo_formula(lowW);
  var   lowIdxAdj = 0: stoIndexT;

  // (Optional) tighten the storage if wholeR
  // does not fall on this locale in the lowest cycle.
  if lowLocNo > locId then
    lowCycNo += 1;
  else
    // (Optional) tighten the storage if wholeR
    // starts on this locale, but not at the beginning of it.
    if lowLocNo == locId then
      lowIdxAdj = _divByStride(_dsiLocOff(lowW));

  return lowCycNo * storagePerCycle:stoIndexT + lowIdxAdj;
}

inline proc BlockCyclic1dom._dsiStorageHigh(locId: locIdT): stoIndexT {
  const hiW = wholeR.high;

  // biggest cycNo(i) for i in wholeR
  var   hiCycNo  = _dsiCycNo(hiW): stoIndexT;
  const hiLocNo  = _dsiLocNo_formula(hiW);
  var   hiIdxAdj = (storagePerCycle - 1): stoIndexT;

  // (Optional) tighten the storage if wholeR
  // does not fall on this locale in the highest cycle.
  if hiLocNo < locId then
    hiCycNo -= 1;
  else
    // (Optional) tighten the storage if wholeR
    // ends on this locale, but not at the end of it.
    if hiLocNo == locId then
      hiIdxAdj = _divByStride(_dsiLocOff(hiW));

  return hiCycNo * storagePerCycle:stoIndexT + hiIdxAdj;
}

proc BlockCyclic1locdom.dsiSetLocalIndices1d(globDD, locId: locIdT): range(stoIndexT) {
  const stoLow = globDD._dsiStorageLow(locId);
  const stoHigh = globDD._dsiStorageHigh(locId);

//proc debugShowD1() {
// if debugD1Shown then return false; debugD1Shown = true; return true;
//}
//if globDD.debugIsD1() && !debugD1Shown then writeln();
//if !globDD.debugIsD1() || debugShowD1() {
//writeln("dsiSetLocalIndices1d ", globDD.name, "  l ", locId,
//        " -> ", stoLow, "..", stoHigh,
//        if stoLow <= stoHigh then "" else "  empty");
//}

  return stoLow:stoIndexT .. stoHigh:stoIndexT;
}

proc BlockCyclic1dom.dsiStorageUsesUserIndices() param return false;

proc BlockCyclic1dom.dsiAccess1d(ind: idxType): (locIdT, stoIndexT) {
  return (_dsiLocNo_formula(ind), _dsiStorageIdx(ind));
}

iter BlockCyclic1locdom.dsiMyDensifiedRangeForSingleTask1d(globDD) {
// todo: for the special case handled in dsiMyDensifiedRangeForTaskID1d,
// maybe handling it here will be beneficial, too?
  const locNo = this.locId;
  const wholeR = globDD.wholeR;
  const lowIdx = wholeR.low;
  const highIdx = wholeR.high;
  type retT = dsiMyDensifiedRangeType1d(globDD);
  param stridable = globDD.stridable;
  assert(stridable == wholeR.stridable); // sanity

  if lowIdx > highIdx then
    return;

  const lowCycNo = globDD._dsiCycNo(lowIdx);
  const highCycNo = globDD._dsiCycNo(highIdx);
  const up = wholeR.stride > 0;
  assert(lowIdx <= highIdx);

  proc mydensify(densifyee): dsiMyDensifiedRangeType1d(globDD) {
    const temp = densify(densifyee, wholeR);
    return temp:retT;
  }

  var curIndices = globDD._dsiIndicesOnCycLoc(
                            if up then lowCycNo else highCycNo, locNo);

  const firstRange = wholeR[curIndices];
  if firstRange.low <= firstRange.high then
    yield mydensify(firstRange);
  // else nothing to yield on this locale

  if lowCycNo == highCycNo then
    // we have covered all cycles
    return;

  proc advance() {
    curIndices = curIndices.translate(
      if !stridable || up then globDD.cycleSizePos else -globDD.cycleSizePos);
  }

  for cycNo in (lowCycNo + 1) .. (highCycNo - 1) {
    advance();
    const curRange =
      if stridable then curIndices by wholeR.stride align wholeR.alignment
      else              curIndices;
    yield mydensify(curRange);
  }

  advance();
  const lastRange = wholeR[curIndices];
  if lastRange.low <= lastRange.high then
    yield mydensify(lastRange);
}

// available in a special case only, for now
proc BlockCyclic1dom.dsiSingleTaskPerLocaleOnly1d()
  return !BlockCyclicDim_allowParLeader ||
         !((blockSizePos:wholeR.stride.type) == wholeR.stride);

// only works when BlockCyclic1dom.dsiSingleTaskPerLocaleOnly1d()
proc BlockCyclic1locdom.dsiMyDensifiedRangeForTaskID1d(globDD, taskid:int, numTasks:int)
{
  const wholeR = globDD.wholeR;
  const nLocs  = globDD.numLocalesPos :globDD.idxType;
  assert((globDD.blockSizePos:wholeR.stride.type) == wholeR.stride);
  assert(globDD.storagePerCycle == 1); // should follow from the previous

  // In this case, the densified range for *all* indices on this locale is:
  //   0..#wholeR.length by numLocales align AL
  // where
  //   (_dsiLocNo(wholeR.low) + AL) % numLocales == this.locId

  const firstLoc = globDD._dsiLocNo_formula(wholeR.low);
  const AL = this.locId + nLocs - firstLoc;

  // Here is the densified range for all indices on this locale.
  const hereDenseInds = 0..#wholeR.length by nLocs align AL;
  const hereNumInds   = hereDenseInds.length;
  const hereFirstInd  = hereDenseInds.first;

  // This is our piece of hereNumInds
  const (begNo,endNo) = _computeChunkStartEnd(hereNumInds, numTasks, taskid+1);

  // Pick the corresponding part of hereDenseInds
  const begIx = hereFirstInd + (begNo - 1) * nLocs;
  const endIx = hereFirstInd + (endNo - 1) * nLocs;
  assert(hereDenseInds.member(begIx));
  assert(hereDenseInds.member(endIx));

//writeln("MyDensifiedRangeForTaskID(", globDD.name, ") on ", locId,
//        "  taskid ", taskid, " of ", numTasks, "  ", begIx, "...", endIx,
//        "   fl=", firstLoc, " al=", AL,
//        "  fullR ", hereDenseInds, " myR ", begIx .. endIx by nLocs,
//        "");

  return begIx .. endIx by nLocs;
}

proc BlockCyclic1locdom.dsiMyDensifiedRangeType1d(globDD) type
  return range(idxType=globDD.idxType, stridable=globDD.stridable);

proc BlockCyclic1locdom.dsiLocalSliceStorageIndices1d(globDD, sliceRange)
  : range(stoIndexT, sliceRange.boundedType, false)
{
  if sliceRange.stridable {
    // to be done: figure out sliceRange's stride vs. globDD.wholeR.stride
    compilerError("localSlice is not implemented for the Dimensional distribution with a block-cyclic dimension specifier when the slice is stridable");
  } else {
    const lowSid = if sliceRange.hasLowBound()
      then globDD._dsiStorageIdx(sliceRange.low)
      else 0: stoIndexT;
    const highSid = if sliceRange.hasHighBound()
      then globDD._dsiStorageIdx(sliceRange.high)
      else 0: stoIndexT;
    return new range(stoIndexT, sliceRange.boundedType, false, lowSid, highSid);
  }
}

iter BlockCyclic1dom.dsiSerialArrayIterator1d() {
  // dispatch here, for code clarity
  if stridable then
    for result in _dsiSerialArrayIterator1dStridable() do
      yield result;
  else
    for result in _dsiSerialArrayIterator1dUnitstride(wholeR) do
      yield result;
}

iter BlockCyclic1dom._dsiSerialArrayIterator1dUnitstride(rangeToIterateOver) {
  assert(!rangeToIterateOver.stridable);

  const firstIdx = rangeToIterateOver.low;
  const lastIdx = rangeToIterateOver.high;

  // This rarely fires, but if so it gets rid of lots of computations.
  // In the common case it adds just 1 branch to at least 2 branches.
  if firstIdx > lastIdx then return;

  // the current point, initialized to the starting point
  var cycNo = _dsiCycNo(firstIdx);
  var locNo = _dsiLocNo_formula(firstIdx);
  var locOff = _dsiLocOff(firstIdx);

  // the final point
  const lastCycNo = _dsiCycNo(lastIdx);
  const lastLocNo = _dsiLocNo_formula(lastIdx);
  const lastLocOff = _dsiLocOff(lastIdx);

  // shortcut
  proc spec(start, end)  return
    (locNo, _dsiStorageIdx2(cycNo, start): stoIndexT ..
             _dsiStorageIdx2(cycNo, end): stoIndexT);

  assert(cycNo <= lastCycNo);
  assert(locNo < numLocalesPos);
  while cycNo < lastCycNo {
    while locNo < numLocalesPos {
      yield spec(locOff, blockSizePos - 1);
      locNo += 1;
      locOff = 0;
    }
    cycNo += 1;
    locNo = 0;
  }

  assert(cycNo == lastCycNo);
  assert(locNo <= lastLocNo);
  while locNo < lastLocNo {
    yield spec(locOff, blockSizePos - 1);
    locNo += 1;
    locOff = 0;
  }

  assert(cycNo == lastCycNo);
  assert(locNo == lastLocNo);
  assert(locOff <= lastLocOff);
  yield spec(locOff, lastLocOff);
}

iter BlockCyclic1dom._dsiSerialArrayIterator1dStridable() {
  assert(stridable);
 if BlockCyclicDim_enableArrayIterWarning then
  compilerWarning("array iterator over stridable block-cyclic-dim arrays is presently not efficient", 4);

  // the simplest way out
  for ind in wholeR do
    yield (_dsiLocNo_formula(ind), _dsiStorageIdx(ind)..#(1:stoIndexT));
}

iter BlockCyclic1dom.dsiFollowerArrayIterator1d(undensRange): (locIdT, idxType) {
  if undensRange.stridable {
    // the simplest way out
    for ix in undensRange do
      yield dsiAccess1d(ix);

  } else {
    for (locNo, stoIxs) in _dsiSerialArrayIterator1dUnitstride(undensRange) do
      for stoIdx in stoIxs do
        yield (locNo, stoIdx);
  }
}