#pragma once
#include <string>
#include <optional>

// Returns true  if the user (by Microsoft OID) is a member of the given group.
// Returns false if explicitly not a member.
// Returns nullopt if the check could not be performed (missing env vars, network error, etc.).
// Requires env vars: AZURE_TENANT_ID, AZURE_CLIENT_ID, AZURE_CLIENT_SECRET
// The app registration needs the "GroupMember.Read.All" application permission granted.
std::optional<bool> is_group_member(const std::string &user_oid, const std::string &group_id);
