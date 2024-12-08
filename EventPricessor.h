class IEventProcessor
{
public:
    //////////////////////////////////////////////////////////////////////////
    /// ---
    struct ReservedEvent
    {
        ReservedEvent(void);
        ReservedEvent(const Integer sequence_number, void *const event);

        ReservedEvent(const ReservedEvent &) = delete;
        ReservedEvent &operator=(const ReservedEvent &) = delete;

        ReservedEvent(ReservedEvent &&) = delete;
        ReservedEvent &operator=(ReservedEvent &&) = delete;

        Integer GetSequenceNumber(void) const { return sequence_number_; }
        void *GetEvent(void) const { return event_; }

    private:
        const Integer sequence_number_;
        void *const event_;
    };

    //////////////////////////////////////////////////////////////////////////
    /// ---
    struct ReservedEvents
    {
        ReservedEvents(const Integer sequence_number, void *const event, const size_t count, const size_t event_size);
        ReservedEvents(const ReservedEvents &) = delete;
        ReservedEvents &operator=(const ReservedEvents &) = delete;

        ReservedEvents(ReservedEvents &&) = delete;
        ReservedEvents &operator=(ReservedEvents &&) = delete;

        template <class TEvent, template <class> class Constructor, class... Args>
        void Emplace(const size_t index, Args &&...args)
        {
            void *const event = GetEvent(index);
            if (event)
                Constructor<TEvent>::Construct(event, std::forward<Args>(args)...);
        }

        template <class TEvent, class... Args>
        void Emplace(const size_t index, Args &&...args)
        {
            void *const event = GetEvent(index);

            if (event)
                NewPlacementConstructor<TEvent>::Construct(event, std::forward<Args>(args)...);
        }

        Integer GetSequenceNumber(void) const
        {
            return sequence_number_;
        }
        void *const GetEvent(const size_t index) const;

        size_t Count(void) const
        {
            return count_;
        }

    private:
        const Integer sequence_number_;
        void *const events_;
        const size_t count_;
        const size_t event_size_;
    };

    //////////////////////////////////////////////////////////////////////////
    /// ---
    template <class T, template <class> class Constructor, class... Args>
    ReservedEvent Reserve(Args &&...args)
    {
        const auto reservation = ReserveEvent();
        if (!reservation.second)
            return ReservedEvent();

        Constructor<T>::Construct(reservation.second, std::forward<Args>(args)...);
        return ReservedEvent(reservation.first, reservation.second);
    }

    //////////////////////////////////////////////////////////////////////////
    /// ---
    template <class T, class... Args>
    ReservedEvent Reserve(Args &&...args)
    {
        const auto reservation = ReserveEvent();
        if (!reservation.second)
            return ReservedEvent();

        NewPlacementConstructor<T>::Construct(reservation.second, std::forward<Args>(args)...);
        return ReservedEvent(reservation.first, reservation.second);
    }

    //////////////////////////////////////////////////////////////////////////
    /// ---
    std::vector<ReservedEvents> ReserveRange(const size_t size);

    //////////////////////////////////////////////////////////////////////////
    /// ---
    void Commit(const Integer sequence_number);

 
        void
        Commit(const Integer sequence_number, const size_t count);
}

class Event : public IEvent
{
public:
    explicit Event(const int value) : value_(value) {}
    virtual ~Event(void) override = default;

    Event(const Event &) = delete;
    Event &operator=(const Event &) = delete;

    Event(Event &&) = delete;
    Event &operator=(Event &&) = delete;

    virtual void Process(void) override
    {
        // do something with value_
    }

private:
    int value_;
}
