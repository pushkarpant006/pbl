
function calculateRiskScore(matchedRules) {
  let riskScore = 0;
  for (let index = 0; index < matchedRules.length; index += 1) {
    riskScore += Number(matchedRules[index].severity_score);
  }
  let threatLevel = "SAFE";
  if (riskScore > 0 && riskScore < 40) threatLevel = "LOW";
  else if (riskScore < 70 && riskScore >= 40) threatLevel = "MEDIUM";
  else if (riskScore >= 70) threatLevel = "HIGH";
  return { riskScore, threatLevel };
}

function estimateRiskFromFindings(findings) {
  let riskScore = 0;
  for (let index = 0; index < findings.length; index += 1) {
    if (findings[index].type === "TOO_MANY_PROCESSES") riskScore += 40;
    else if (findings[index].type === "TIMEOUT") riskScore += 30;
  }
  let threatLevel = "SAFE";
  if (riskScore > 0 && riskScore < 40) threatLevel = "LOW";
  else if (riskScore >= 40 && riskScore < 70) threatLevel = "MEDIUM";
  else if (riskScore >= 70) threatLevel = "HIGH";
  return { riskScore, threatLevel };
}

module.exports = { calculateRiskScore, estimateRiskFromFindings };
