#pragma once
class ClientInstance;
namespace MyClientJavaChat {
bool Enable(); void Disable();
void JsonProfileApplied(bool hidden);
void Tick(ClientInstance& client);
}
