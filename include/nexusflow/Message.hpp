#ifndef NEXUSFLOW_MESSAGE_HPP
#define NEXUSFLOW_MESSAGE_HPP

#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace nexusflow {

/**
 * @class SharedMessage
 * @brief A type-erased, thread-safe, shared message container for the pipeline.
 *
 * This class can hold an object of any type and allows for efficient, shared
 * ownership of the contained data across multiple threads. When a SharedMessage is
 *  copied, it creates a new SharedMessage object that shares ownership of the same
 * underlying data, using a shared_ptr internally. This makes it ideal for
 * broadcast scenarios in a multi-threaded pipeline.
 */
class SharedMessage {
public:
    SharedMessage() noexcept = default;

    /**
     * @brief Constructs a SharedMessage with the given data.
     * @param data The data to store in the SharedMessage.
     * @tparam T The type of the data.
     * @note The type T must be copyable or movable.
     * @note This constructor uses perfect forwarding to avoid unnecessary copies.
     * @note This constructor is only enabled if T is not a SharedMessage, is copyable or movable, and is not a reference or pointer.
     */
    template <typename T, typename = typename std::enable_if_t<!std::is_same<typename std::decay_t<T>, SharedMessage>::value &&
                                                               (std::is_copy_constructible<typename std::decay_t<T>>::value ||
                                                                std::is_move_constructible<typename std::decay_t<T>>::value) &&
                                                               (!std::is_reference<typename std::decay_t<T>>::value &&
                                                                !std::is_pointer<typename std::decay_t<T>>::value)>>

    SharedMessage(T&& data) {
        static_assert(std::is_copy_constructible<typename std::decay<T>::type>::value ||
                          std::is_move_constructible<typename std::decay<T>::type>::value,
                      "Type must be copyable or movable");

        static_assert(!std::is_reference<typename std::decay<T>::type>::value && !std::is_pointer<typename std::decay<T>::type>::value,
                      "Type must not be a reference or pointer");

        m_content = std::make_shared<Model<typename std::decay<T>::type>>(std::forward<T>(data));
    }

    // --- Copy, Move, and Default Operations ---
    // The default copy/move/destructor work perfectly because m_content is a shared_ptr.
    SharedMessage(const SharedMessage& other) = default;
    SharedMessage& operator=(const SharedMessage& other) = default;
    SharedMessage(SharedMessage&& other) noexcept = default;
    SharedMessage& operator=(SharedMessage&& other) noexcept = default;
    ~SharedMessage() = default;

    // --- Accessors ---
    inline bool HasData() const { return m_content != nullptr; }

    template <typename T>
    inline bool HasData() const {
        return HasData() && typeid(T) == m_content->getTypeInfo();
    }

    // --- Data Access ---
    template <typename T>
    void SetData(T&& data) {
        m_content = std::make_shared<Model<typename std::decay<T>::type>>(std::forward<T>(data));
    }

    // --- Pointer-based access ---
    template <typename T>
    T* GetData() {
        if (!HasData<T>()) {
            return nullptr;
        }
        // Safely downcast from Concept to Model<T> and get the data pointer.
        return &static_cast<Model<T>*>(m_content.get())->m_data;
    }

    template <typename T>
    const T* GetData() const {
        if (!HasData<T>()) {
            return nullptr;
        }
        // Safely downcast from Concept to Model<T> and get the data pointer.
        return &static_cast<const Model<T>*>(m_content.get())->m_data;
    }

    // --- Shared Pointer-based access ---
    template <typename T>
    std::shared_ptr<T> GetSharedData() const {
        if (!HasData<T>()) {
            return nullptr;
        }
        const auto* model = static_cast<const Model<T>*>(m_content.get());
        return std::shared_ptr<T>(m_content, &model->m_data);
    }

    // --- Reference-based access ---
    template <typename T>
    const T& GetDataRef() const {
        if (!HasData() || typeid(T) != m_content->getTypeInfo()) {
            throw std::runtime_error("Type mismatch or empty message in getDataRef (const)");
        }
        return static_cast<const Model<T>*>(m_content.get())->m_data;
    }

    template <typename T>
    T& GetDataRef() {
        auto&& typeInfo = typeid(T);
        if (!HasData() || typeInfo != m_content->getTypeInfo()) {
            throw std::runtime_error("No data of type " + std::string(typeInfo.name()) + " in message.");
        }
        return static_cast<Model<T>*>(m_content.get())->m_data;
    }

private:
    // --- Internal Type-Erasure Implementation ---
    /**
     * @struct Concept
     * @brief The abstract base class for our type-erasure mechanism (the "Concept").
     *
     * It defines the common interface for all concrete type holders.
     */
    struct Concept {
        virtual ~Concept() = default;
        virtual const std::type_info& getTypeInfo() const noexcept = 0;

        // A helper to get the raw data pointer for the aliasing constructor.
        virtual void* getRawDataPtr() = 0;
    };

    /**
     * @struct Model<T>
     * @brief The templated concrete class that holds the actual data (the "Model").
     */
    template <typename T>
    struct Model final : Concept {
        // ⭐ [OPTIMIZATION] The model now directly holds the data object,
        // not a shared_ptr to it. The entire Model<T> object will be managed
        // by the outer shared_ptr.
        explicit Model(T data) : m_data(std::move(data)) {}

        const std::type_info& getTypeInfo() const noexcept override { return typeid(T); }

        void* getRawDataPtr() override { return &m_data; }

        T m_data; // The actual data is stored here.
    };

    // The single shared_ptr that manages the lifetime of the internal Model object.
    std::shared_ptr<Concept> m_content;
};

} // namespace nexusflow

#endif // NEXUSFLOW_MESSAGE_HPP