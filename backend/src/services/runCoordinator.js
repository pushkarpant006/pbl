
let runIsActive = false;

function tryAcquireRun() {
  if (runIsActive) return null;
  runIsActive = true;
  let released = false;
  return function releaseRun() {
    if (released) return;
    released = true;
    runIsActive = false;
  };
}

module.exports = { tryAcquireRun };
