#ifndef NEXUSFLOW_MESSAGE_HPP
#define NEXUSFLOW_MESSAGE_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace nexusflow {

struct MessageMeta {
    uint64_t messageId; // The unique identifier for the message
    uint64_t timstamp; // The timestamp when the message was created
    std::string sourceName; // The name of the source of the message
};

/**
 * @class Message
 * @brief A type-erased, thread-safe, shared message container for the pipeline.
 *
 * This class can hold an object of any type and allows for efficient, shared
 * ownership of the contained data across multiple threads. When a Message is
 *  copied, it creates a new Message object that shares ownership of the same
 * underlying data, using a shared_ptr internally. This makes it ideal for
 * broadcast scenarios in a multi-threaded pipeline.
 */
class Message {
public:
    Message() noexcept = default;

    /**
     * @brief Constructs a Message with the given data.
     * @param data The data to store in the Message.
     * @tparam T The type of the data.
     * @note The type T must be copyable or movable.
     * @note This constructor uses perfect forwarding to avoid unnecessary copies.
     * @note This constructor is only enabled if T is not a Message, is copyable or movable, and is not a reference or pointer.
     */
    template <typename T, typename DT = typename std::decay<T>::type,
              typename = std::enable_if_t<!std::is_same<DT, Message>::value &&
                                          (std::is_copy_constructible<DT>::value || std::is_move_constructible<DT>::value) &&
                                          (!std::is_reference<DT>::value && !std::is_pointer<DT>::value)>>
    Message(T&& data, std::string sourceName = "") {
        static_assert(std::is_copy_constructible<DT>::value || std::is_move_constructible<DT>::value,
                      "Type must be copyable or movable");

        static_assert(!std::is_reference<DT>::value && !std::is_pointer<DT>::value, "Type must not be a reference or pointer");

        // Create a shared_ptr to a Model<T> containing the data
        m_content = std::make_shared<Model<DT>>(std::forward<T>(data));

        // Create a unique message ID and timestamp
        static std::atomic<uint64_t> messageIdCounter{0};
        uint64_t messageId = messageIdCounter.fetch_add(1);
        auto now = std::chrono::steady_clock::now();
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        m_metaData = MessageMeta{messageId, timestamp, std::move(sourceName)}; // Initialize with default values
    }

    // --- Copy, Move, and Default Operations ---
    // The default copy/move/destructor work perfectly because m_content is a shared_ptr.
    Message(const Message& other) = default;
    Message& operator=(const Message& other) = default;
    Message(Message&& other) noexcept = default;
    Message& operator=(Message&& other) noexcept = default;
    ~Message() = default;

    // --- Clone ---
    Message Clone() const {
        if (!HasData()) return {};

        // Create a new Message object with the same data and metadata
        Message clone;
        clone.m_content = m_content->Clone();
        clone.m_metaData = m_metaData;
        return clone;
    }

    // --- Accessors ---
    inline bool HasData() const { return m_content != nullptr; }

    template <typename T>
    inline bool HasData() const {
        return HasData() && typeid(T) == m_content->getTypeInfo();
    }

    inline MessageMeta GetMetaData() const { return m_metaData; }

    MessageMeta& MetaData() { return m_metaData; }

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
        virtual void* getRawDataPtr() = 0;
        virtual std::shared_ptr<Concept> Clone() const = 0;
    };

    /**
     * @struct Model<T>
     * @brief The templated concrete class that holds the actual data (the "Model").
     */
    template <typename T>
    struct Model final : Concept {
        explicit Model(T data) : m_data(std::move(data)) {}

        const std::type_info& getTypeInfo() const noexcept override { return typeid(T); }

        void* getRawDataPtr() override { return &m_data; }

        std::shared_ptr<Concept> Clone() const override {
            return std::make_shared<Model<T>>(m_data); // Copy m_data
        }

        T m_data; // The actual data is stored here.
    };

    // The single shared_ptr that manages the lifetime of the internal Model object.
    std::shared_ptr<Concept> m_content;
    MessageMeta m_metaData;
};

} // namespace nexusflow

#endif // NEXUSFLOW_MESSAGE_HPP