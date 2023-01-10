
#include <array>
#include <iostream>

#include <pxr/usd/kind/registry.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/propertySpec.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdf/variantSetSpec.h>
#include <pxr/usd/sdf/variantSpec.h>
#include <pxr/usd/usd/prim.h>

#include "Commands.h"
#include "CompositionEditor.h"
#include "Constants.h"
#include "FileBrowser.h"
#include "ImGuiHelpers.h"
#include "ModalDialogs.h"
#include "ProxyHelpers.h"
#include "SdfLayerEditor.h"
#include "SdfPrimEditor.h"
#include "Shortcuts.h"
#include "TableLayouts.h"
#include "VariantEditor.h"
#include "VtDictionaryEditor.h"
#include "VtValueEditor.h"

//// NOTES: Sdf API: Removing a variantSet and cleaning it from the list editing
//// -> https://groups.google.com/g/usd-interest/c/OeqtGl_1H-M/m/xjCx3dT9EgAJ

struct CreateAttributeDialog : public ModalDialog {
    CreateAttributeDialog(const SdfPrimSpecHandle &sdfPrim) : _sdfPrim(sdfPrim){};
    ~CreateAttributeDialog() override {}

    void Draw() override {
        ImGui::InputText("Name", &_attributeName);
        if (ImGui::BeginCombo("Type", _typeName.GetAsToken().GetString().c_str())) {
            for (int i = 0; i < GetAllValueTypeNames().size(); i++) {
                if (ImGui::Selectable(GetAllValueTypeNames()[i].GetAsToken().GetString().c_str(), false)) {
                    _typeName = GetAllValueTypeNames()[i];
                }
            }
            ImGui::EndCombo();
        }
        bool varying = _variability == SdfVariabilityVarying;
        if (ImGui::Checkbox("Varying", &varying)) {
            _variability = _variability == SdfVariabilityVarying ? SdfVariabilityUniform : SdfVariabilityVarying;
        }
        ImGui::Checkbox("Custom", &_custom);
        ImGui::Checkbox("Create default value", &_createDefault);
        DrawOkCancelModal([&]() {
            ExecuteAfterDraw<PrimCreateAttribute>(_sdfPrim, _attributeName, _typeName, _variability, _custom, _createDefault);
        });
    }
    const char *DialogId() const override { return "Create attribute"; }

    const SdfPrimSpecHandle &_sdfPrim;
    std::string _attributeName;
    SdfVariability _variability = SdfVariabilityVarying;
    SdfValueTypeName _typeName = SdfValueTypeNames->Bool;
    bool _custom = true;
    bool _createDefault = false;
};

struct CreateRelationDialog : public ModalDialog {
    CreateRelationDialog(const SdfPrimSpecHandle &sdfPrim) : _sdfPrim(sdfPrim){};
    ~CreateRelationDialog() override {}

    void Draw() override {
        ImGui::InputText("Relationship name", &_relationName);
        ImGui::InputText("Target path", &_targetPath);
        if (ImGui::BeginCombo("Edit list", GetListEditorOperationName(_operation))) {
            for (int i = 0; i < GetListEditorOperationSize(); ++i) {
                if (ImGui::Selectable(GetListEditorOperationName(i), false)) {
                    _operation = i;
                }
            }
            ImGui::EndCombo();
        }

        bool varying = _variability == SdfVariabilityVarying;
        if (ImGui::Checkbox("Varying", &varying)) {
            _variability = _variability == SdfVariabilityVarying ? SdfVariabilityUniform : SdfVariabilityVarying;
        }
        ImGui::Checkbox("Custom", &_custom);

        DrawOkCancelModal([=]() {
            ExecuteAfterDraw<PrimCreateRelationship>(_sdfPrim, _relationName, _variability, _custom, _operation, _targetPath);
        });
    }
    const char *DialogId() const override { return "Create relationship"; }

    const SdfPrimSpecHandle &_sdfPrim;
    std::string _relationName;
    std::string _targetPath;
    int _operation = 0;
    SdfVariability _variability = SdfVariabilityVarying;
    bool _custom = true;
};

struct CreateVariantModalDialog : public ModalDialog {

    CreateVariantModalDialog(const SdfPrimSpecHandle &primSpec) : _primSpec(primSpec){};
    ~CreateVariantModalDialog() override {}

    void Draw() override {
        if (!_primSpec) {
            CloseModal();
            return;
        }
        bool isVariant = _primSpec->GetPath().IsPrimVariantSelectionPath();
        ImGui::InputText("VariantSet name", &_variantSet);
        ImGui::InputText("Variant name", &_variant);
        if (isVariant) {
            ImGui::Checkbox("Add to variant edit list", &_addToEditList);
        }
        //
        if (ImGui::Button("Add")) {
            // TODO The call might not be safe as _primSpec is copied, so create an actual command instead
            std::function<void()> func = [=]() {
                SdfCreateVariantInLayer(_primSpec->GetLayer(), _primSpec->GetPath(), _variantSet, _variant);
                if (isVariant && _addToEditList) {
                    auto ownerPath = _primSpec->GetPath().StripAllVariantSelections();
                    // This won't work on doubly nested variants,
                    // when there is a Prim between the variants
                    auto ownerPrim = _primSpec->GetPrimAtPath(ownerPath);
                    if (ownerPrim) {
                        auto nameList = ownerPrim->GetVariantSetNameList();
                        if (!nameList.ContainsItemEdit(_variantSet)) {
                            ownerPrim->GetVariantSetNameList().Add(_variantSet);
                        }
                    }
                }
            };
            ExecuteAfterDraw<UsdFunctionCall>(_primSpec->GetLayer(), func);
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            CloseModal();
        }
    }

    const char *DialogId() const override { return "Add variant"; }
    SdfPrimSpecHandle _primSpec;
    std::string _variantSet;
    std::string _variant;
    bool _addToEditList = false;
};

/// Very basic ui to create a connection
struct CreateSdfAttributeConnectionDialog : public ModalDialog {

    CreateSdfAttributeConnectionDialog(SdfAttributeSpecHandle attribute) : _attribute(attribute){};
    ~CreateSdfAttributeConnectionDialog() override {}

    void Draw() override {
        if (!_attribute)
            return;
        ImGui::Text("Create connection for %s", _attribute->GetPath().GetString().c_str());
        if (ImGui::BeginCombo("Edit list", GetListEditorOperationName(_operation))) {
            for (int i = 0; i < GetListEditorOperationSize(); ++i) {
                if (ImGui::Selectable(GetListEditorOperationName(i), false)) {
                    _operation = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::InputText("Path", &_connectionEndPoint);
        DrawOkCancelModal(
            [&]() { ExecuteAfterDraw<PrimCreateAttributeConnection>(_attribute, _operation, _connectionEndPoint); });
    }
    const char *DialogId() const override { return "Attribute connection"; }

    SdfAttributeSpecHandle _attribute;
    std::string _connectionEndPoint;
    int _operation = 0;
};

void DrawPrimSpecifier(const SdfPrimSpecHandle &primSpec, ImGuiComboFlags comboFlags) {

    const SdfSpecifier current = primSpec->GetSpecifier();
    SdfSpecifier selected = current;
    const std::string specifierName = TfEnum::GetDisplayName(current);
    if (ImGui::BeginCombo("Specifier", specifierName.c_str(), comboFlags)) {
        for (int n = SdfSpecifierDef; n < SdfNumSpecifiers; n++) {
            const SdfSpecifier displayed = static_cast<SdfSpecifier>(n);
            const bool isSelected = (current == displayed);
            if (ImGui::Selectable(TfEnum::GetDisplayName(displayed).c_str(), isSelected)) {
                selected = displayed;
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
        }

        if (selected != current) {
            ExecuteAfterDraw(&SdfPrimSpec::SetSpecifier, primSpec, selected);
        }

        ImGui::EndCombo();
    }
}

// TODO Share code as we want to share the style of the button, but not necessarily the behaviour
// DrawMiniButton ?? in a specific file ? OptionButton ??? OptionMenuButton ??
static void DrawPropertyMiniButton(const char *btnStr, int rowId, const ImVec4 &btnColor = ImVec4({0.0, 0.7, 0.0, 1.0})) {
    ImGui::PushID(rowId);
    ImGui::PushStyleColor(ImGuiCol_Text, btnColor);
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::SmallButton(btnStr);
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::PopID();
}

void DrawPrimInstanceableActionButton(const SdfPrimSpecHandle &primSpec, int buttonId) {
    DrawPropertyMiniButton("(m)", buttonId);
    if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
        if (primSpec->HasInstanceable()) {
            if (ImGui::Button("Reset to default")) {
                ExecuteAfterDraw(&SdfPrimSpec::ClearInstanceable, primSpec);
            }
        }
        ImGui::EndPopup();
    }
}

void DrawPrimInstanceable(const SdfPrimSpecHandle &primSpec) {
    if (!primSpec)
        return;
    bool isInstanceable = primSpec->GetInstanceable();
    if (ImGui::Checkbox("Instanceable", &isInstanceable)) {
        ExecuteAfterDraw(&SdfPrimSpec::SetInstanceable, primSpec, isInstanceable);
    }
}

void DrawPrimHidden(const SdfPrimSpecHandle &primSpec) {
    if (!primSpec)
        return;
    bool isHidden = primSpec->GetHidden();
    if (ImGui::Checkbox("Hidden", &isHidden)) {
        ExecuteAfterDraw(&SdfPrimSpec::SetHidden, primSpec, isHidden);
    }
}

void DrawPrimActive(const SdfPrimSpecHandle &primSpec) {
    if (!primSpec)
        return;
    bool isActive = primSpec->GetActive();
    if (ImGui::Checkbox("Active", &isActive)) {
        // TODO: use CTRL click to clear the checkbox
        ExecuteAfterDraw(&SdfPrimSpec::SetActive, primSpec, isActive);
    }
}

void DrawPrimName(const SdfPrimSpecHandle &primSpec) {
    auto nameBuffer = primSpec->GetName();
    ImGui::InputText("Prim Name", &nameBuffer);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        auto primName = std::string(const_cast<char *>(nameBuffer.data()));
        if (primSpec->CanSetName(primName, nullptr)) {
            ExecuteAfterDraw(&SdfPrimSpec::SetName, primSpec, primName, true);
        }
    }
}

void DrawPrimKind(const SdfPrimSpecHandle &primSpec) {
    auto primKind = primSpec->GetKind();
    if (ImGui::BeginCombo("Kind", primKind.GetString().c_str())) {
        for (auto kind : KindRegistry::GetAllKinds()) {
            bool isSelected = primKind == kind;
            if (ImGui::Selectable(kind.GetString().c_str(), isSelected)) {
                ExecuteAfterDraw(&SdfPrimSpec::SetKind, primSpec, kind);
            }
        }
        ImGui::EndCombo();
    }
}

/// Convert prim class tokens to and from char *
/// The chars are stored in DrawPrimType
static inline const char *ClassCharFromToken(const TfToken &classToken) {
    return classToken == SdfTokens->AnyTypeToken ? "" : classToken.GetString().c_str();
}

static inline TfToken ClassTokenFromChar(const char *classChar) {
    return strcmp(classChar, "") == 0 ? SdfTokens->AnyTypeToken : TfToken(classChar);
}

/// Draw a prim type name combo
void DrawPrimType(const SdfPrimSpecHandle &primSpec, ImGuiComboFlags comboFlags) {
    const char *currentItem = ClassCharFromToken(primSpec->GetTypeName());
    const auto &allSpecTypes = GetAllSpecTypeNames();
    static int selected = 0;

    if (ComboWithFilter("Prim Type", currentItem, allSpecTypes, &selected, comboFlags)) {
        const auto newSelection = allSpecTypes[selected].c_str();
        if (primSpec->GetTypeName() != ClassTokenFromChar(newSelection)) {
            ExecuteAfterDraw(&SdfPrimSpec::SetTypeName, primSpec, ClassTokenFromChar(newSelection));
        }
    }
}
template <typename T> static inline void DrawArrayEditorButton(T attribute) {
    if ((*attribute)->GetDefaultValue().IsArrayValued()) {
        if (ImGui::Button(ICON_FA_LIST)) {
            ExecuteAfterDraw<EditorSelectAttributePath>((*attribute)->GetPath());
        }
        ImGui::SameLine();
    }
}

inline SdfPathEditorProxy GetPathEditorProxy(SdfSpecHandle spec, TfToken field) {
#ifdef WIN32
    // Unfortunately on windows the SdfGetPathEditorProxy is not exposed
    // So the following code is a workaround
    //Only two calls for the moment, with the arguments:
    //attribute, SdfFieldKeys->ConnectionPaths
    //relation, SdfFieldKeys->TargetPaths
    if (spec->GetSpecType() == SdfSpecTypeAttribute && field == SdfFieldKeys->ConnectionPaths) {
        SdfAttributeSpecHandle attr = spec->GetLayer()->GetAttributeAtPath(spec->GetPath());
        return attr->GetConnectionPathList();
    } else if (spec->GetSpecType() == SdfSpecTypeRelationship && field == SdfFieldKeys->TargetPaths) {
        SdfRelationshipSpecHandle rel = spec->GetLayer()->GetRelationshipAtPath(spec->GetPath());
        return rel->GetTargetPathList();
    }
    return {}; // Shouldn't happen, if it does, add the case above
#else
return SdfGetPathEditorProxy(spec, field);
#endif
}


// TODO: move this code and generalize it for any Path edit list
// SdfFieldKeys->ConnectionPaths
static void DrawEditListOneLineEditor(SdfSpecHandle spec, TfToken field) {
    SdfPathEditorProxy proxy = GetPathEditorProxy(spec, field);
    ImGuiContext &g = *GImGui;
    ImGuiWindow *window = g.CurrentWindow;
    ImGuiStorage *storage = window->DC.StateStorage;
    // We want to show the first non empty list.
    constexpr int editListCount = 6;
    const char *editListNames[editListCount] = {"Ex", "Or", "Ap", "Ad", "Pr", "De"};
    int currentList = 0;

    // TODO getEditList should go in ProxyHelpers, but need some refactoring to add Ordered
    auto getEditList = [](SdfPathEditorProxy &editList, int which) {
        // could use a table with pointer to function, but at the stage of writing it might not be
        // worth the time
        if (which == 1) {
            return editList.GetOrderedItems();
        } else if (which == 2) {
            return editList.GetAppendedItems();
        } else if (which == 3) {
            return editList.GetAddedItems();
        } else if (which == 4) {
            return editList.GetPrependedItems();
        } else if (which == 5) {
            return editList.GetDeletedItems();
        }
        return editList.GetExplicitItems();
    };

    // Did we already kwow which list we want to show ?
    const auto key = ImGui::GetItemID();
    currentList = storage->GetInt(key, -1);
    if (currentList == -1) { // select the non empty list or explicit
        currentList = 0;
        for (int i = 0; i < 6; i++) {
            if (!getEditList(proxy, i).empty()) {
                currentList = i;
                break;
            }
        }
        storage->SetInt(key, currentList);
    }

    // Edit list chooser
    ImGui::SmallButton(editListNames[currentList]);
    if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
        for (int i = 0; i < editListCount; ++i) {
            // Changing the color to show the empty lists
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  getEditList(proxy, i).empty() ? ImVec4(0.5, 0.5, 0.5, 1.0) : ImVec4(1.0, 1.0, 1.0, 1.0));
            if (ImGui::MenuItem(editListNames[i])) {
                storage->SetInt(key, i);
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    thread_local std::string itemsString; // avoid reallocating
    itemsString.clear();
    for (const SdfPath &item : getEditList(proxy, currentList)) {
        itemsString.append(item.GetString());
        itemsString.append(" "); // we don't care about the last space, it also helps the user adding a new item
    }

    ImGui::InputText("##EditListOneLineEditor", &itemsString);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::vector<std::string> newList = TfStringSplit(itemsString, " ");
        // TODO: should the following code go in a command ??
        std::function<void()> updateList = [=]() {
            if (spec) {
                auto editorProxy = GetPathEditorProxy(spec, field);
                auto editList = getEditList(editorProxy, currentList);
                editList.clear();
                for (const auto &path : newList) {
                    editList.push_back(SdfPath(path));
                }
            }
        };
        ExecuteAfterDraw<UsdFunctionCall>(spec->GetLayer(), updateList);
    }
}

// static void DrawAttributeConnectionEditListItem(const char *operation, const SdfPath &path, int &id,
//                                                const SdfAttributeSpecHandle &attribute) {
//    //    ImGui::Selectable(path.GetText());
//    //    if (ImGui::BeginPopupContextItem(nullptr)) {
//    //        if(ImGui::MenuItem("Remove connection")) {
//    //            ExecuteAfterDraw<PrimRemoveAttributeConnection>(attribute, path);
//    //        }
//    //        ImGui::EndPopup();
//    //    }
//    ImGui::PushID(path.GetText());
//    ImGui::Text("%s", operation);
//    ImGui::SameLine();
//    std::string newPathStr = path.GetString();
//    ImGui::InputText("###AttributeConnection", &newPathStr);
//    if (ImGui::IsItemDeactivatedAfterEdit()) {
//        if (newPathStr.empty()) {
//            std::function<void()> removeItem = [=]() {
//                if (attribute) {
//                    attribute->GetConnectionPathList().RemoveItemEdits(path);
//                }
//            };
//            ExecuteAfterDraw<UsdFunctionCall>(attribute->GetLayer(), removeItem);
//        } else {
//            std::function<void()> replaceItemEdits = [=]() {
//                if (attribute) {
//                    attribute->GetConnectionPathList().ReplaceItemEdits(path, SdfPath(newPathStr));
//                }
//            };
//            ExecuteAfterDraw<UsdFunctionCall>(attribute->GetLayer(), replaceItemEdits);
//        }
//    }
//    // TODO we might also want to be able to edit what the connection is pointing
//    // ImGui::Text("%s", path.GetText());
//    ImGui::PopID();
//}

struct AttributeRow {};
// TODO:
// ICON_FA_EDIT could be great for edit target
// ICON_FA_DRAW_POLYGON great for meshes
template <>
inline ScopedStyleColor GetRowStyle<AttributeRow>(const int rowId, const SdfAttributeSpecHandle &attribute,
                                                  const Selection &selection, const int &showWhat) {
    const bool selected = selection.IsSelected(attribute);
    ImVec4 colorSelected = selected ? ImVec4(ColorAttributeSelectedBg) : ImVec4(0.75, 0.60, 0.33, 0.2);
    return ScopedStyleColor(ImGuiCol_HeaderHovered, selected ? colorSelected : ImVec4(ColorTransparent), ImGuiCol_HeaderActive,
                            ImVec4(ColorTransparent), ImGuiCol_Header, colorSelected, ImGuiCol_Text,
                            ImVec4(ColorAttributeAuthored), ImGuiCol_Button, ImVec4(ColorTransparent), ImGuiCol_FrameBg,
                            ImVec4(ColorEditableWidgetBg));
}

template <>
inline void DrawFirstColumn<AttributeRow>(const int rowId, const SdfAttributeSpecHandle &attribute, const Selection &selection,
                                          const int &showWhat) {
    ImGui::PushID(rowId);

    ImGui::Button(ICON_FA_CARET_SQUARE_DOWN);
    if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
        if (ImGui::MenuItem(ICON_FA_TRASH " Remove attribute")) {
            // attribute->GetLayer()->GetAttributeAtPath(attribute)
            SdfPrimSpecHandle primSpec = attribute->GetLayer()->GetPrimAtPath(attribute->GetOwner()->GetPath());
            ExecuteAfterDraw(&SdfPrimSpec::RemoveProperty, primSpec, primSpec->GetPropertyAtPath(attribute->GetPath()));
        }
        if (!attribute->HasConnectionPaths() && ImGui::MenuItem(ICON_FA_LINK " Create connection")) {
            DrawModalDialog<CreateSdfAttributeConnectionDialog>(attribute);
        }

        // Only if there are no default
        if (!attribute->HasDefaultValue() && ImGui::MenuItem(ICON_FA_PLUS " Create default value")) {
            std::function<void()> createDefaultValue = [=]() {
                if (attribute) {
                    auto defaultValue = attribute->GetTypeName().GetDefaultValue();
                    attribute->SetDefaultValue(defaultValue);
                }
            };
            ExecuteAfterDraw<UsdFunctionCall>(attribute->GetLayer(), createDefaultValue);
        }
        if (attribute->HasDefaultValue() && ImGui::MenuItem(ICON_FA_TRASH " Clear default value")) {
            std::function<void()> clearDefaultValue = [=]() {
                if (attribute) {
                    attribute->ClearDefaultValue();
                }
            };
            ExecuteAfterDraw<UsdFunctionCall>(attribute->GetLayer(), clearDefaultValue);
        }

        if (ImGui::MenuItem(ICON_FA_KEY " Add key value")) {
            // DrawModalDialog<>(attribute);
        }

        if (ImGui::MenuItem(ICON_FA_COPY " Copy property")) {
            ExecuteAfterDraw<PropertyCopy>(static_cast<SdfPropertySpecHandle>(attribute));
        }

        if (ImGui::MenuItem(ICON_FA_COPY " Copy property path")) {
            ImGui::SetClipboardText(attribute->GetPath().GetString().c_str());
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
};

template <>
inline void DrawSecondColumn<AttributeRow>(const int rowId, const SdfAttributeSpecHandle &attribute, const Selection &selection,
                                           const int &showWhat) {
    // Still not sure we want to show the type at all or in the same column as the name
    ImGui::Text("%s (%s)", attribute->GetName().c_str(), attribute->GetTypeName().GetAsToken().GetText());
};

template <>
inline void DrawThirdColumn<AttributeRow>(const int rowId, const SdfAttributeSpecHandle &attribute, const Selection &selection,
                                          const int &showWhat) {
    // Check what to show, this could be stored in a variable ... check imgui
    // For the mini buttons: ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
    ImGui::PushID(rowId);
    bool selected = selection.IsSelected(attribute);
    if (ImGui::Selectable("##select", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
        ExecuteAfterDraw<EditorSelectAttributePath>(attribute->GetPath());
    }
    ImGui::SameLine();
    if (attribute->HasDefaultValue()) {
        // Show Default value if any
        ImGui::PushItemWidth(-FLT_MIN);
        VtValue modified = DrawVtValue("##Default", attribute->GetDefaultValue());
        if (modified != VtValue()) {
            ExecuteAfterDraw(&SdfPropertySpec::SetDefaultValue, attribute, modified);
        }
    }

    // TODO: draw a key or something if the attribute has time samples
    //    if (!timeSamples.empty()) {
    // ImGui::Button(ICON_FA_KEY);
    //    }

    if (attribute->HasConnectionPaths()) {
        ScopedStyleColor connectionColor(ImGuiCol_Text, ImVec4(ColorAttributeConnection));
        ImGui::SmallButton(ICON_FA_LINK); // TODO: add delete list or "make explicit" menu
        ImGui::SameLine();
        SdfConnectionsProxy connections = attribute->GetConnectionPathList();
        DrawEditListOneLineEditor(attribute, SdfFieldKeys->ConnectionPaths);
    }
    ImGui::PopID();
    // ImGui::PopStyleVar(1);
};

void DrawPrimSpecAttributes(const SdfPrimSpecHandle &primSpec, const Selection &selection) {
    if (!primSpec)
        return;

    const auto &attributes = primSpec->GetAttributes();
    if (attributes.empty())
        return;
    if (ImGui::CollapsingHeader("Attributes", ImGuiTreeNodeFlags_DefaultOpen)) {
        int rowId = 0;
        if (BeginThreeColumnsTable("##DrawPrimSpecAttributes")) {
            SetupThreeColumnsTable(false, "", "Attribute", "");
            // the third column allows to show different attribute properties:
            // Default value, keyed values or connections (and summary ??)
            // int showWhat = DrawValueColumnSelector();
            int showWhat = 0;
            for (const SdfAttributeSpecHandle &attribute : attributes) {
                DrawThreeColumnsRow<AttributeRow>(rowId++, attribute, selection, showWhat);
            }
            ImGui::EndTable();
        }
    }
}

// static void DrawRelationshipEditListItem(const char *operation, const SdfPath &path, int &id, const SdfPrimSpecHandle
// &primSpec,
//                                         SdfPath &relationPath) {
//    std::string newPathStr = path.GetString();
//    ImGui::PushID(id++);
//    ImGui::Text("%s", operation);
//    ImGui::SameLine();
//    ImGui::PushItemWidth(-FLT_MIN);
//    ImGui::InputText("###RelationValue", &newPathStr);
//    if (ImGui::IsItemDeactivatedAfterEdit()) {
//        std::function<void()> replaceItemEdits = [=]() {
//            if (primSpec) {
//                const auto &relationship = primSpec->GetRelationshipAtPath(relationPath);
//                if (relationship) {
//                    relationship->GetTargetPathList().ReplaceItemEdits(path, SdfPath(newPathStr));
//                }
//            }
//        };
//        ExecuteAfterDraw<UsdFunctionCall>(primSpec->GetLayer(), replaceItemEdits);
//    }
//    ImGui::PopItemWidth();
//    ImGui::PopID();
//}

struct RelationRow {};
template <>
inline void DrawSecondColumn<RelationRow>(const int rowId, const SdfPrimSpecHandle &primSpec,
                                          const SdfRelationshipSpecHandle &relation) {
    ImGui::Text("%s", relation->GetName().c_str());
};
template <>
inline void DrawThirdColumn<RelationRow>(const int rowId, const SdfPrimSpecHandle &primSpec,
                                         const SdfRelationshipSpecHandle &relation) {
    ImGui::PushItemWidth(-FLT_MIN);
    DrawEditListOneLineEditor(relation, SdfFieldKeys->TargetPaths);
};
template <>
inline void DrawFirstColumn<RelationRow>(const int rowId, const SdfPrimSpecHandle &primSpec,
                                         const SdfRelationshipSpecHandle &relation) {
    if (ImGui::Button(ICON_FA_TRASH)) {
        ExecuteAfterDraw(&SdfPrimSpec::RemoveProperty, primSpec, primSpec->GetPropertyAtPath(relation->GetPath()));
    }
};

void DrawPrimSpecRelations(const SdfPrimSpecHandle &primSpec) {
    if (!primSpec)
        return;
    const auto &relationships = primSpec->GetRelationships();
    if (relationships.empty())
        return;
    if (ImGui::CollapsingHeader("Relations", ImGuiTreeNodeFlags_DefaultOpen)) {
        int rowId = 0;
        if (BeginThreeColumnsTable("##DrawPrimSpecRelations")) {
            SetupThreeColumnsTable(false, "", "Relations", "");
            auto relations = primSpec->GetRelationships();
            for (const SdfRelationshipSpecHandle &relation : relations) {
                DrawThreeColumnsRow<RelationRow>(rowId, primSpec, relation);
            }
            ImGui::EndTable();
        }
    }
}

#define GENERATE_FIELD(ClassName_, FieldName_, DrawFunction_)                                                                    \
    struct ClassName_ {                                                                                                          \
        static constexpr const char *fieldName = FieldName_;                                                                     \
    };                                                                                                                           \
    template <> inline void DrawThirdColumn<ClassName_>(const int rowId, const SdfPrimSpecHandle &primSpec) {                    \
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);                                                               \
        DrawFunction_(primSpec);                                                                                                 \
    }

#define GENERATE_FIELD_WITH_BUTTON(ClassName_, Token_, FieldName_, DrawFunction_)                                                \
    GENERATE_FIELD(ClassName_, FieldName_, DrawFunction_);                                                                       \
    template <> inline bool HasEdits<ClassName_>(const SdfPrimSpecHandle &prim) { return prim->HasField(Token_); }               \
    template <> inline void DrawFirstColumn<ClassName_>(const int rowId, const SdfPrimSpecHandle &primSpec) {                    \
        ImGui::PushID(rowId);                                                                                                    \
        if (ImGui::Button(ICON_FA_TRASH) && HasEdits<ClassName_>(primSpec)) {                                                    \
            ExecuteAfterDraw(&SdfPrimSpec::ClearField, primSpec, Token_);                                                        \
        }                                                                                                                        \
        ImGui::PopID();                                                                                                          \
    }

GENERATE_FIELD(Specifier, "Specifier", DrawPrimSpecifier);
GENERATE_FIELD(PrimType, "Type", DrawPrimType);
GENERATE_FIELD(PrimName, "Name", DrawPrimName);
GENERATE_FIELD_WITH_BUTTON(PrimKind, SdfFieldKeys->Kind, "Kind", DrawPrimKind);
GENERATE_FIELD_WITH_BUTTON(PrimActive, SdfFieldKeys->Active, "Active", DrawPrimActive);
GENERATE_FIELD_WITH_BUTTON(PrimInstanceable, SdfFieldKeys->Instanceable, "Instanceable", DrawPrimInstanceable);
GENERATE_FIELD_WITH_BUTTON(PrimHidden, SdfFieldKeys->Hidden, "Hidden", DrawPrimHidden);

void DrawPrimSpecMetadata(const SdfPrimSpecHandle &primSpec) {
    if (!primSpec->GetPath().IsPrimVariantSelectionPath()) {
        if (ImGui::CollapsingHeader("Core Metadata", ImGuiTreeNodeFlags_DefaultOpen)) {
            int rowId = 0;
            if (BeginThreeColumnsTable("##DrawPrimSpecMetadata")) {
                SetupThreeColumnsTable(false, "", "Metadata", "Value");
                DrawThreeColumnsRow<Specifier>(rowId++, primSpec);
                DrawThreeColumnsRow<PrimType>(rowId++, primSpec);
                DrawThreeColumnsRow<PrimName>(rowId++, primSpec);
                DrawThreeColumnsRow<PrimKind>(rowId++, primSpec);
                DrawThreeColumnsRow<PrimActive>(rowId++, primSpec);
                DrawThreeColumnsRow<PrimInstanceable>(rowId++, primSpec);
                DrawThreeColumnsRow<PrimHidden>(rowId++, primSpec);
                DrawThreeColumnsDictionaryEditor<SdfPrimSpec>(rowId, primSpec, SdfFieldKeys->CustomData);
                DrawThreeColumnsDictionaryEditor<SdfPrimSpec>(rowId, primSpec, SdfFieldKeys->AssetInfo);
                EndThreeColumnsTable();
            }
            ImGui::Separator();
        }
    }
}

static void DrawPrimAssetInfo(const SdfPrimSpecHandle prim) {
    if (!prim)
        return;
    const auto &assetInfo = prim->GetAssetInfo();
    if (!assetInfo.empty()) {
        if (ImGui::CollapsingHeader("Asset Info",
                                    ImGuiTreeNodeFlags_DefaultOpen)) { // This should really go in the metadata header
            if (ImGui::BeginTable("##DrawAssetInfo", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
                TableSetupColumns("", "Asset Info", "");
                ImGui::TableHeadersRow();
                TF_FOR_ALL(keyValue, assetInfo) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", keyValue->first.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushItemWidth(-FLT_MIN);
                    VtValue modified = DrawVtValue(keyValue->first.c_str(), keyValue->second);
                    if (modified != VtValue()) {
                        ExecuteAfterDraw(&SdfPrimSpec::SetAssetInfo, prim, keyValue->first, modified);
                    }
                    ImGui::PopItemWidth();
                }
                ImGui::EndTable();
                ImGui::Separator();
            }
        }
    }
}

void DrawPrimCreateCompositionMenu(const SdfPrimSpecHandle &primSpec) {
    if (primSpec) {
        if (ImGui::MenuItem("Reference")) {
            DrawPrimCreateReference(primSpec);
        }
        if (ImGui::MenuItem("Payload")) {
            DrawPrimCreatePayload(primSpec);
        }
        if (ImGui::MenuItem("Inherit")) {
            DrawPrimCreateInherit(primSpec);
        }
        if (ImGui::MenuItem("Specialize")) {
            DrawPrimCreateSpecialize(primSpec);
        }
        if (ImGui::MenuItem("Variant")) {
            DrawModalDialog<CreateVariantModalDialog>(primSpec);
        }
    }
}

void DrawPrimCreatePropertyMenu(const SdfPrimSpecHandle &primSpec) {
    if (primSpec) {
        if (ImGui::MenuItem("Attribute")) {
            DrawModalDialog<CreateAttributeDialog>(primSpec);
        }
        if (ImGui::MenuItem("Relation")) {
            DrawModalDialog<CreateRelationDialog>(primSpec);
        }
    }
}

void DrawSdfPrimEditorMenuBar(const SdfPrimSpecHandle &primSpec) {

    bool enabled = primSpec;
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("New", enabled)) {
            DrawPrimCreateCompositionMenu(primSpec);
            ImGui::Separator();
            DrawPrimCreatePropertyMenu(primSpec); // attributes and relation
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit", enabled)) {
            if (ImGui::MenuItem("Paste")) {
                ExecuteAfterDraw<PropertyPaste>(primSpec);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void DrawSdfPrimEditor(const SdfPrimSpecHandle &primSpec, const Selection &selection) {
    if (!primSpec)
        return;
    auto headerSize = ImGui::GetWindowSize();
    headerSize.y = TableRowDefaultHeight * 3; // 3 fields in the header
    headerSize.x = -FLT_MIN;
    ImGui::BeginChild("##LayerHeader", headerSize);
    DrawSdfLayerIdentity(primSpec->GetLayer(), primSpec->GetPath()); // TODO rename to DrawUsdObjectInfo()
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::BeginChild("##LayerBody");
    DrawPrimSpecMetadata(primSpec);
    DrawPrimAssetInfo(primSpec);

    DrawPrimCompositions(primSpec);
    DrawPrimVariants(primSpec);
    DrawPrimSpecAttributes(primSpec, selection);
    DrawPrimSpecRelations(primSpec);
    ImGui::EndChild();
    if (ImGui::IsItemHovered()) {
        const SdfPath &selectedProperty = selection.GetAnchorPropertyPath(primSpec->GetLayer());
        if (selectedProperty != SdfPath()) {
            SdfPropertySpecHandle selectedPropertySpec = primSpec->GetLayer()->GetPropertyAtPath(selectedProperty);
            AddShortcut<PropertyCopy, ImGuiKey_LeftCtrl, ImGuiKey_C>(selectedPropertySpec);
        }
        AddShortcut<PropertyPaste, ImGuiKey_LeftCtrl, ImGuiKey_V>(primSpec);
    }
}
