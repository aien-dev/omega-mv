# LAUNCHES: what launching 436 GPU jobs per token costs (L137, roadblock R4)

One decode token of the 30B model is one list of 436 GPU jobs. This folder holds:
- PREDICTION.md: what we expected, written before measuring.
- FUSION_MAP.md: which neighbouring jobs could be merged so their hand-off never touches memory, with bytes per token.
- launch-tax-*/: the chip receipts (real list, same list with empty jobs, one empty job alone). Results: RESULT.md.
