function visitFormElements(node, visitor) {
    // Only consider the elements that accept user input and have a name.
    if (node.nodeType == 1 && node.name && 
        (node.type == "text" || node.type == "textarea" || node.type == "select-one" || 
         node.type == "number"))
        visitor(node)
    // Recurse ...
    const children = node.children;
    for (let child of children)
        visitFormElements(child, visitor);
}

// Takes a JSON object and populates the form field values. If the 
// form contains an element that is not in the JSON object that element
// is disabled.
function setFormFromData(rootNode, data) {
    visitFormElements(rootNode, el => {
        if (el.name in data) {
            el.value = data[el.name];
        } else {
            el.value = null;
            el.disabled = true;
        }
    });
}

function getDataFromForm(rootNode) {
    const formData = new FormData(rootNode);
    const formObject = Object.fromEntries(formData.entries());
    result = {};
    for (name in formObject)
        result[name] = formObject[name].trim();
    return result;
}

function setupForm(node) {
    //visitFormElements(node, el => {
    //    console.log("Node", el.name, "Tag", el.tagName);
    //});
    setFormFromData(node, { "node": "xxx" });
    console.log(getDataFromForm(node))
}
