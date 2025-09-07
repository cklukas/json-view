### ✅ Developer To-Do List

The following tasks remain open.

-----

### 1\. Use Smart Pointers for Automatic Memory Management 🧠

  * **What to change:** The manual `new Node()` and `delete n` is error-prone. Modern C++ offers a safer way.
  * **How to change:** Use `std::unique_ptr` for automatic, exception-safe memory management (**RAII**).
    1.  In the `Node` struct, change `std::vector<Node *> children;` to `std::vector<std::unique_ptr<Node>> children;`.
    2.  Your `buildTree` function should now return a `std::unique_ptr<Node>`. Use `auto node = std::make_unique<Node>();` for allocation.
    3.  When adding a child, use `node->children.push_back(std::move(child));`.
    4.  **Crucially, you can now delete the entire manual cleanup loop** at the end of `main()`. Memory will be managed automatically.

 
