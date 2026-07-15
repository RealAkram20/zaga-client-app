#pragma once

#include <windows.h>
#include <credentialprovider.h>

namespace zaga {

enum FieldId {
    FIELD_TILE_IMAGE = 0,
    FIELD_ACCOUNT,
    FIELD_STATUS,
    FIELD_META,
    FIELD_CODE,
    FIELD_SUBMIT,
    FIELD_MESSAGE,
    FIELD_HELP_LINK,
    FIELD_TECH_LINK,
    FIELD_COUNT,
};

struct FieldStatePair {
    CREDENTIAL_PROVIDER_FIELD_STATE state;
    CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE interactiveState;
};

static const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR c_fieldDescriptors[FIELD_COUNT] = {
    {FIELD_TILE_IMAGE, CPFT_TILE_IMAGE, const_cast<LPWSTR>(L"Zaga Device Lock")},
    {FIELD_ACCOUNT, CPFT_LARGE_TEXT, const_cast<LPWSTR>(L"Account")},
    {FIELD_STATUS, CPFT_SMALL_TEXT, const_cast<LPWSTR>(L"Status")},
    {FIELD_META, CPFT_SMALL_TEXT, const_cast<LPWSTR>(L"Device")},
    {FIELD_CODE, CPFT_EDIT_TEXT, const_cast<LPWSTR>(L"Unlock code")},
    {FIELD_SUBMIT, CPFT_SUBMIT_BUTTON, const_cast<LPWSTR>(L"Unlock")},
    {FIELD_MESSAGE, CPFT_SMALL_TEXT, const_cast<LPWSTR>(L"Message")},
    {FIELD_HELP_LINK, CPFT_COMMAND_LINK, const_cast<LPWSTR>(L"How do I get a code?")},
    {FIELD_TECH_LINK, CPFT_COMMAND_LINK, const_cast<LPWSTR>(L"Technician access")},
};

static const FieldStatePair c_fieldStatePairs[FIELD_COUNT] = {
    {CPFS_DISPLAY_IN_BOTH, CPFIS_NONE},
    {CPFS_DISPLAY_IN_BOTH, CPFIS_NONE},
    {CPFS_DISPLAY_IN_BOTH, CPFIS_NONE},
    {CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE},
    {CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_FOCUSED},
    {CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE},
    {CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE},
    {CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE},
    {CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE},
};

}
