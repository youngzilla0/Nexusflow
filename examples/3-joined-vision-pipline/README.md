# Joined Vision Pipeline Example

This example demonstrates explicit multi-input fusion with named ports.

## Port Wiring

`HeadPersonFusion` uses:

- `head`
- `person`

Those ports are defined in [config.yaml](/Users/yang/Code/Nexusflow/examples/3-joined-vision-pipline/config.yaml):

```yaml
- from: HeadDetector
  to: HeadPersonFusion
  toPort: head

- from: PersonDetector
  to: HeadPersonFusion
  toPort: person
```

## Trigger Policy

The fusion module sets:

```cpp
SetTriggerPolicy(TriggerPolicy::OnAllInputs);
```

So the executor waits until both ports have the same `messageId` before calling `Process()`.

## Why This Example Matters

It shows the intended usage of the new API:

- ports are explicit
- fusion is explicit
- scheduling policy is explicit
