# Inference-cost benchmark: held-out evaluation of verified programs produced per unit of inference cost.
# Picked up by `-include mk/*.mk`.
ifndef INFERENCE_COST_MK
INFERENCE_COST_MK := 1
.PHONY: test-inference-cost

test-inference-cost:
	sh bench/inference_cost/driver.sh --self-test
endif
