#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define CACHE_CAPACITY 10 // Max number of items in the cache
#define HASH_SIZE 256 // Hash table size

typedef struct cache_item {
	char *key;
	char *value;
	struct cache_item *prev, *next;
	struct cache_item *hnext;
} cache_item;

typedef struct cache {
	cache_item *hash_table[HASH_SIZE];
	cache_item *head, *tail;
	pthread_mutex_t lock;
	int size;
} cache;

void insert_at_head(cache *c, cache_item *i)
{
	i->next = c->head;
	i->prev = NULL;

	if (c->head)
		c->head->prev = i;

	c->head = i;

	if (!c->tail)
		c->tail = i;
}
void move_to_front(cache *c, cache_item *i)
{
	// If it's at head already, we are good.
	if (i == c->head)
		return;

	// Unlink the item from the list.
	if (i->prev)
		i->prev->next = i->next;

	if (i->next)
		i->next->prev = i->prev;

	// If it's at tail, update the tail to the previous
	// item.
	if (i == c->tail)
		c->tail = i->prev;

	insert_at_head(c, i);
}

unsigned int hash(const char *key)
{
	unsigned int hash = 0;

	while (*key) {
		hash = (hash << 5) + *key++;
	}

	return hash % HASH_SIZE;
}

void evict(cache *c)
{
	if (!c)
		return;

	cache_item *evict_item = c->tail;

	unsigned int idx = hash(evict_item->key);
	cache_item *prev = NULL, *curr = c->hash_table[idx];

	// Remove from the hash map chain
	while (curr && curr != evict_item) {
		prev = curr;
		curr = curr->hnext;
	}

	if (prev)
		prev->hnext = evict_item->hnext;
	else
		c->hash_table[idx] = curr->hnext;

	if (evict_item->prev)
		evict_item->prev->next = NULL;
	c->tail = evict_item->prev;
	if (c->head == evict_item->prev)
		c->head = NULL;

	free(evict_item->key);
	free(evict_item->value);
	free(evict_item);
	c->size--;
}

void cache_destroy(cache *c)
{
	cache_item *i = c->head;

	while (i) {
		cache_item *next = i->next;
		free(i->key);
		free(i->value);
		free(i);
		i = next;
	}
	pthread_mutex_destroy(&c->lock);
	free(c);
}

const char *cache_get(cache *c, const char *key)
{
	pthread_mutex_lock(&c->lock);
	unsigned int idx = hash(key);
	cache_item *i = c->hash_table[idx];

	while (i) {
		if (strcmp(i->key, key) == 0) {
			move_to_front(c, i);
			pthread_mutex_unlock(&c->lock);
			return i->value;
		}
		i = i->hnext;
	}

	pthread_mutex_unlock(&c->lock);
	return NULL;
}

void cache_put(cache *c, const char *key, const char *value)
{
	pthread_mutex_lock(&c->lock);
	unsigned int idx = hash(key);
	cache_item *i = c->hash_table[idx];

	while (i) {
		// If the key is found, move it to the front.
		if (strcmp(i->key, key) == 0) {
			free(i->value);
			i->value = strdup(value);
			move_to_front(c, i);
			pthread_mutex_unlock(&c->lock);
			return;
		}
		i = i->hnext;
	}

	i = (cache_item *)malloc(sizeof(cache_item));
	i->key = strdup(key);
	i->value = strdup(value);

	i->prev = i->next = NULL;

	i->hnext = c->hash_table[idx];
	c->hash_table[idx] = i;

	insert_at_head(c, i);
	c->size++;
	if (c->size > CACHE_CAPACITY)
		evict(c);

	pthread_mutex_unlock(&c->lock);
}

cache *cache_create()
{
	cache *c = (cache *)malloc(sizeof(cache));
	memset(c->hash_table, 0, sizeof(c->hash_table));
	c->head = NULL;
	c->tail = NULL;
	pthread_mutex_init(&c->lock, NULL);
	return c;
}

// Function to pretty print the cache for debugging
void cache_print(cache *cache)
{
	printf("Cache Size: %d\n", cache->size);
	for (int i = 0; i < HASH_SIZE; i++) {
		if (cache->hash_table[i] != NULL) {
			printf("Index %d:", i);
			cache_item *item = cache->hash_table[i];
			while (item) {
				printf(" -> [Key: %s, Value: %s]", item->key,
				       item->value);
				item = item->hnext;
			}
			printf("\n");
		}
	}

	printf("LRU List (from most recent to least recent):\n");
	cache_item *current = cache->head;
	while (current) {
		printf(" -> [Key: %s, Value: %s]", current->key,
		       current->value);
		current = current->next;
	}

	printf("\n\n");
}

// Example usage in main function
int main()
{
	cache *cache = cache_create();

	cache_put(cache, "key1", "value1");
	cache_put(cache, "key2", "value2");
	cache_put(cache, "key3", "value3");

	// Pretty print the cache
	cache_print(cache);
	cache_put(cache, "key3", "hahahah");
	cache_print(cache);

	cache_destroy(cache);
	return 0;
}