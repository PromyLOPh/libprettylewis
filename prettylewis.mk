PRETTYLEWIS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
PRETTYLEWIS_SRC := $(wildcard $(PRETTYLEWIS_DIR)/src/*.c)
PRETTYLEWIS_INC := -I$(PRETTYLEWIS_DIR)/src
$(info Using prettylewis $(PRETTYLEWIS_DIR))
