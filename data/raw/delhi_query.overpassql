// Delhi NCR metro systems: DMRC, Rapid Metro Gurgaon and Noida Metro (Aqua Line).
// RapidX (Delhi-Meerut RRTS) is regional rail and deliberately excluded.
[out:json][timeout:180][bbox:28.30,76.80,28.90,77.60];
rel["route"="subway"]["network"~"^(Delhi Metro|Rapid Metro Gurgaon|Noida Metro)$"];
out body;
node(r);
out body;
