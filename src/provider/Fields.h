#pragma once

#include <windows.h>
#include <credentialprovider.h>

namespace zaga {

// The locked tile shows exactly what a stranded customer needs and nothing else:
// whose account this is, that payment is due and where to pay, who to call, and the
// box to type the code into. Every extra line on a lock screen is read by someone
// who cannot use their computer — brevity here is a courtesy, not a style choice.
enum FieldId {
    FIELD_TILE_IMAGE = 0,
    FIELD_ACCOUNT,
    FIELD_STATUS,
    FIELD_PURCHASE,
    FIELD_SUPPORT,
    FIELD_CODE,
    FIELD_SUBMIT,
    FIELD_MESSAGE,
    FIELD_COUNT,
};

struct FieldStatePair {
    CREDENTIAL_PROVIDER_FIELD_STATE state;
    CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE interactiveState;
};

static const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR c_fieldDescriptors[FIELD_COUNT] = {
    {FIELD_TILE_IMAGE, CPFT_TILE_IMAGE, const_cast<LPWSTR>(L"Zaga Device Lock")},
    {FIELD_ACCOUNT, CPFT_LARGE_TEXT, const_cast<LPWSTR>(L"Account Number")},
    {FIELD_STATUS, CPFT_SMALL_TEXT, const_cast<LPWSTR>(L"Status")},
    {FIELD_PURCHASE, CPFT_SMALL_TEXT, const_cast<LPWSTR>(L"Payment")},
    {FIELD_SUPPORT, CPFT_SMALL_TEXT, const_cast<LPWSTR>(L"Support")},
    {FIELD_CODE, CPFT_EDIT_TEXT, const_cast<LPWSTR>(L"Unlock code")},
    {FIELD_SUBMIT, CPFT_SUBMIT_BUTTON, const_cast<LPWSTR>(L"Unlock")},
    {FIELD_MESSAGE, CPFT_SMALL_TEXT, const_cast<LPWSTR>(L"Message")},
};

static const FieldStatePair c_fieldStatePairs[FIELD_COUNT] = {
    {CPFS_DISPLAY_IN_BOTH, CPFIS_NONE},              // TILE_IMAGE
    {CPFS_DISPLAY_IN_BOTH, CPFIS_NONE},              // ACCOUNT
    {CPFS_DISPLAY_IN_BOTH, CPFIS_NONE},              // STATUS
    {CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE},     // PURCHASE
    {CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE},     // SUPPORT
    {CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_FOCUSED},  // CODE
    {CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE},     // SUBMIT
    {CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE},     // MESSAGE
};

}
