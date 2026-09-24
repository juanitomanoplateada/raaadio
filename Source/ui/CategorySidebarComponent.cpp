#include "CategorySidebarComponent.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

CategorySidebarComponent::CategorySidebarComponent(MusicLibrary& lib) : library(lib) {
    addAndMakeVisible(categorySearchBox);
    categorySearchBox.setIndents(10, 0);
    categorySearchBox.setJustification(juce::Justification::centredLeft);
    categorySearchBox.setFont(UiStyle::ui(14.0f));
    categorySearchBox.onTextChange = [this] { refreshCategories(); };
    categorySearchBox.onEscapeKey = [this] { categorySearchBox.clear(); refreshCategories(); };

    addAndMakeVisible(categoryList);
    categoryList.setModel(this);
    categoryList.setRowHeight(UiStyle::rowHeight);
    categoryList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);

    addAndMakeVisible(addCategoryButton);
    UiStyle::setIcon(addCategoryButton, "plus");
    addCategoryButton.setButtonText("");
    addAndMakeVisible(newCategoryName);
    newCategoryName.setIndents(10, 0);
    newCategoryName.setJustification(juce::Justification::centredLeft);
    newCategoryName.setFont(UiStyle::ui(14.0f));
    newCategoryName.onReturnKey = [this] { createNewCategory(); };
    addCategoryButton.onClick = [this] { createNewCategory(); };

    refreshCategories();
    lookAndFeelChanged();
}

void CategorySidebarComponent::lookAndFeelChanged() {
    const auto muted = ThemeManager::getInstance().getMutedTextColour();
    categorySearchBox.setTextToShowWhenEmpty(TRANS("filter_categories"), muted);
    newCategoryName.setTextToShowWhenEmpty(TRANS("new_category"), muted);
    addCategoryButton.setTooltip(TRANS("tip_add_category"));

    refreshCategories();
}

CategorySidebarComponent::~CategorySidebarComponent() {
    categoryList.setModel(nullptr);
}

void CategorySidebarComponent::paint(juce::Graphics& g) {
    UiStyle::drawSectionTitle(g, getLocalBounds().removeFromTop(20), TRANS("categories_title"));
}

void CategorySidebarComponent::resized() {
    auto area = getLocalBounds();
    area.removeFromTop(24);

    auto addArea = area.removeFromTop(UiStyle::controlHeight);
    addCategoryButton.setBounds(addArea.removeFromRight(UiStyle::controlHeight));
    addArea.removeFromRight(6);
    newCategoryName.setBounds(addArea);
    area.removeFromTop(6);

    categorySearchBox.setBounds(area.removeFromTop(UiStyle::controlHeight));
    area.removeFromTop(8);

    categoryList.setBounds(area);
}

void CategorySidebarComponent::refreshCategories() {
    allCategories = library.getCategories();
    numSystemCategories = (int) std::count_if(allCategories.begin(), allCategories.end(),
                                              [](const Category& c) { return c.isSystem(); });

    // Se filtra por el nombre que se ve (el de las categorías del sistema está traducido).
    const auto filter = categorySearchBox.getText().trim();
    displayCategories.clear();
    for (const auto& cat : allCategories)
        if (filter.isEmpty() || MusicLibrary::getDisplayName(cat).containsIgnoreCase(filter))
            displayCategories.push_back(cat);

    categoryList.updateContent();
    setSelectedCategory(selectedCategory);
    categoryList.repaint();
}

int CategorySidebarComponent::getNumRows() {
    return static_cast<int>(displayCategories.size()) + 2;
}

const Category* CategorySidebarComponent::categoryAt(int row) const {
    if (row < 2 || row - 2 >= (int) displayCategories.size()) return nullptr;
    return &displayCategories[(size_t) row - 2];
}

void CategorySidebarComponent::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    auto& t = ThemeManager::getInstance();
    auto row = juce::Rectangle<float>((float) width, (float) height).reduced(0.0f, 1.0f);

    if (rowIsSelected) {
        g.setColour(t.getSelectionColour());
        g.fillRoundedRectangle(row, 5.0f);
        g.setColour(t.getHighlightColour());
        g.fillRoundedRectangle(row.withWidth(3.0f).reduced(0.0f, 5.0f), 1.5f);
    }

    const auto* cat = categoryAt(rowNumber);
    juce::String name;
    if (rowNumber == 0) name = TRANS("all_categories");
    else if (rowNumber == 1) name = TRANS("uncategorized");
    else if (cat != nullptr) name = MusicLibrary::getDisplayName(*cat);

    // Las categorías fijas de la emisora llevan un candado.
    const bool system = cat != nullptr && cat->isSystem();
    int textWidth = width - 16;
    if (system) {
        UiStyle::drawIcon(g, "lock", juce::Rectangle<float>((float) width - 26.0f, 0.0f, 18.0f, (float) height)
                                         .withSizeKeepingCentre(13.0f, 13.0f),
                          t.getMutedTextColour());
        textWidth -= 22;
    }

    const bool special = rowNumber < 2;
    g.setColour(rowIsSelected ? t.getPrimaryTextColour() : (special ? t.getSecondaryTextColour() : t.getPrimaryTextColour()));
    g.setFont(special ? UiStyle::semibold(14.0f) : UiStyle::ui(14.0f));
    g.drawText(name, 12, 0, textWidth, height, juce::Justification::centredLeft, true);

    // Líneas bajo "Sin categoría" y bajo la última categoría fija: separan los filtros, las
    // categorías de la emisora y las del usuario.
    const auto* next = categoryAt(rowNumber + 1);
    if (rowNumber == 1 || (system && next != nullptr && !next->isSystem())) {
        g.setColour(t.getOutlineColour());
        g.fillRect(8, height - 1, width - 16, 1);
    }
}

juce::String CategorySidebarComponent::getTooltipForRow(int row) {
    const auto* cat = categoryAt(row);
    return cat != nullptr && cat->isSystem() ? TRANS("tip_system_category") : juce::String();
}

void CategorySidebarComponent::listBoxItemClicked(int rowNumber, const juce::MouseEvent& e) {
    const auto* cat = categoryAt(rowNumber);

    if (e.mods.isPopupMenu()) {
        // Las categorías de la emisora no se renombran ni se eliminan.
        if (cat == nullptr || cat->isSystem()) return;

        const juce::String name(cat->name);
        juce::PopupMenu m;
        m.addItem(1, TRANS("rename"));
        m.addItem(2, TRANS("menu_delete_category"));
        m.showMenuAsync(juce::PopupMenu::Options(), [this, name](int result) {
            if (result == 1) renameCategory(name);
            else if (result == 2) confirmDeleteCategory(name);
        });
        return;
    }

    if (rowNumber == 0) selectCategory("All Categories");
    else if (rowNumber == 1) selectCategory("Uncategorized");
    else if (cat != nullptr) selectCategory(cat->name);
}

void CategorySidebarComponent::listBoxItemDoubleClicked(int rowNumber, const juce::MouseEvent&) {
    const auto* found = categoryAt(rowNumber);
    if (found == nullptr) return;

    const auto cat = *found;
    auto* w = new juce::AlertWindow(TRANS("dialog_folder_format_title"),
                                    TRANS("dialog_folder_format_msg") + " " + MusicLibrary::getDisplayName(cat),
                                    juce::MessageBoxIconType::QuestionIcon);

    w->addButton(TRANS("fmt_artist"), 1);
    w->addButton(TRANS("fmt_date"), 0);
    w->addButton(TRANS("cancel"), -1);

    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, cat](int result) {
        if (result == 0 || result == 1) {
            if (library.setCategoryFolderFormat(cat.name, result)) {
                refreshCategories();
            }
        }
        delete w;
    }));
}

void CategorySidebarComponent::deleteKeyPressed(int lastRowSelected) {
    const auto* cat = categoryAt(lastRowSelected);
    if (cat != nullptr && !cat->isSystem())
        confirmDeleteCategory(cat->name);
}

void CategorySidebarComponent::returnKeyPressed(int lastRowSelected) {

}

void CategorySidebarComponent::selectCategory(const juce::String& name) {
    selectedCategory = name;
    setSelectedCategory(name);
    if (onCategorySelected) onCategorySelected(name);
}

void CategorySidebarComponent::createNewCategory() {
    auto name = library.forceUpperCase(newCategoryName.getText().trim());
    if (name.isNotEmpty()) {
        if (library.addCategory(name)) {
            newCategoryName.clear();
            refreshCategories();
        }
    }
}

void CategorySidebarComponent::renameCategory(const juce::String& oldName) {
    auto* w = new juce::AlertWindow(TRANS("dialog_rename_category"), TRANS("dialog_enter_new_name") + " " + oldName, juce::MessageBoxIconType::QuestionIcon);

    w->addTextEditor("name", oldName);
    w->addButton(TRANS("rename"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton(TRANS("cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, oldName](int result) {
        if (result == 1) {
            juce::String newName = library.forceUpperCase(w->getTextEditorContents("name"));
            if (newName.isNotEmpty() && newName != oldName && library.renameCategory(oldName, newName)) {
                // Si era la categoría a la vista, la biblioteca sigue mostrándola.
                if (selectedCategory == oldName) selectCategory(newName);
                else refreshCategories();
            }
        }
        delete w;
    }));
}

void CategorySidebarComponent::confirmDeleteCategory(const juce::String& name) {
    const int tracks = (int) library.search("", name).size();
    juce::Component::SafePointer<CategorySidebarComponent> safeThis(this);
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, TRANS("dialog_delete_category"),
        TRANS("dialog_delete_category_msg").replace("%1", name).replace("%2", juce::String(tracks)),
        TRANS("btn_delete_category"), TRANS("cancel"), this,
        juce::ModalCallbackFunction::create([safeThis, name](int answer) {
            if (answer != 1 || safeThis == nullptr || !safeThis->library.deleteCategory(name)) return;
            // Se vuelve a filtrar: sus pistas pudieron quedar sin categoría.
            safeThis->selectCategory(safeThis->selectedCategory == name ? juce::String("All Categories")
                                                                        : safeThis->selectedCategory);
        }));
}

juce::var CategorySidebarComponent::getDragSourceDescription(const juce::SparseSet<int>& selectedRows) {
    if (selectedRows.isEmpty()) return {};
    const int row = selectedRows[0];

    // Las categorías de la emisora no se mueven; con el filtro activo no se reordena.
    const auto* cat = categoryAt(row);
    if (cat == nullptr || cat->isSystem() || categorySearchBox.getText().isNotEmpty()) return {};

    return "category_drag_" + juce::String(row - 2);
}

bool CategorySidebarComponent::isInterestedInDragSource(const SourceDetails& dragSourceDetails) {
    return dragSourceDetails.description.toString().startsWith("category_drag_");
}

int CategorySidebarComponent::dropRowFor(const SourceDetails& dragSourceDetails) {
    const auto listPoint = dragSourceDetails.localPosition - categoryList.getPosition();
    int row = categoryList.getRowContainingPosition(listPoint.x, listPoint.y);

    // Debajo de la última fila: al final.
    if (row == -1) {
        int totalHeight = getNumRows() * categoryList.getRowHeight();
        if (auto* vp = categoryList.getViewport()) totalHeight -= vp->getViewPositionY();
        if (listPoint.y >= totalHeight) row = getNumRows();
    }

    // Nunca por encima de las categorías de la emisora, que van siempre primero.
    const int lastRow = getNumRows();
    return juce::jlimit(juce::jmin(2 + numSystemCategories, lastRow), lastRow, row);
}

void CategorySidebarComponent::itemDragMove(const SourceDetails& dragSourceDetails) {
    if (!isInterestedInDragSource(dragSourceDetails)) return;

    const int row = dropRowFor(dragSourceDetails);
    if (row != insertionIndex) {
        insertionIndex = row;
        repaint();
    }
}

void CategorySidebarComponent::itemDragExit(const SourceDetails&) {
    insertionIndex = -1;
    repaint();
}

void CategorySidebarComponent::itemDropped(const SourceDetails& dragSourceDetails) {
    if (!isInterestedInDragSource(dragSourceDetails)) return;

    int targetIndex = dropRowFor(dragSourceDetails) - 2;
    insertionIndex = -1;
    repaint();

    const int sourceIndex = dragSourceDetails.description.toString().fromLastOccurrenceOf("_", false, false).getIntValue();
    if (sourceIndex < numSystemCategories || sourceIndex >= (int) allCategories.size()) return;

    // Al sacarla de la lista, las de abajo suben una fila.
    if (targetIndex > sourceIndex) --targetIndex;
    if (targetIndex == sourceIndex) return;

    const auto item = allCategories[(size_t) sourceIndex];
    allCategories.erase(allCategories.begin() + sourceIndex);
    allCategories.insert(allCategories.begin() + targetIndex, item);

    juce::StringArray names;
    for (const auto& c : allCategories) names.add(c.name);
    library.updateCategoryOrder(names);

    selectCategory(item.name);
}

void CategorySidebarComponent::paintOverChildren(juce::Graphics& g) {
    if (insertionIndex >= 2) {
        g.setColour(ThemeManager::getInstance().getAccentColour());

        int rowHeight = categoryList.getRowHeight();
        int y = categoryList.getY() + (insertionIndex * rowHeight);

        auto* viewport = categoryList.getViewport();
        int scrollY = viewport ? viewport->getViewPositionY() : 0;

        y -= scrollY;

        g.drawLine(0.0f, (float)y, (float)getWidth(), (float)y, 2.0f);

        g.fillEllipse(0.0f, y - 3.0f, 6.0f, 6.0f);
        g.fillEllipse((float)getWidth() - 6.0f, y - 3.0f, 6.0f, 6.0f);
    }
}

void CategorySidebarComponent::setSelectedCategory(const juce::String& categoryName) {
    selectedCategory = categoryName.isEmpty() ? juce::String("All Categories") : categoryName;

    // Sin desplazar la lista: esto también corre cada vez que la biblioteca se refresca.
    if (selectedCategory == "All Categories") {
        categoryList.selectRow(0, true);
    } else if (selectedCategory == "Uncategorized") {
        categoryList.selectRow(1, true);
    } else {
        for (int i = 0; i < (int) displayCategories.size(); ++i) {
            if (juce::String(displayCategories[(size_t) i].name) == selectedCategory) {
                categoryList.selectRow(i + 2, true);
                return;
            }
        }
        categoryList.deselectAllRows();  // oculta por el filtro de categorías
    }
}
